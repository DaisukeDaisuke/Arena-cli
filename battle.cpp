#include "game.h"
#include "lcg.hpp"
#include <sstream>

namespace arena {
    const std::array<EffectFn, 10> effect_jump = {
        damage_handler(), heal_handler(), apply_handler(), clear_handler(), restore_mp_handler(),
        conditional_add_handler(), drain_handler(), spend_bank_handler(), schedule_handler(), borrow_handler()
    };

    void event(RunState &r, const std::string &s) {
        r.log.push_back(s);
        if (r.log.size() > 4) r.log.pop_front();
    }

    int skill_id(const std::string &name) {
        for (int i = 0; i < 20; ++i) if (name == skills[i].name) return i;
        return -1;
    }

    namespace {
        bool has(const Fighter &f, int id) { return std::find(f.slots.begin(), f.slots.end(), id) != f.slots.end(); }

        void phase(EffectContext &c, Phase p) {
            for (const auto &e: skills[c.skill].effects)
                if (e.phase == p) {
                    if (c.opponent.hp == 0 && e.op != Opcode::Drain) break;
                    effect_jump[static_cast<std::size_t>(e.op)](c, e);
                }
        }

        bool direct(int id) {
            for (const auto &e: skills[id].effects) if (e.op == Opcode::Damage) return true;
            return false;
        }

        using ConditionFn = bool (*)(const RunState &);
        const std::array<ConditionFn, 9> conditions = {
            +[](const RunState &) { return true; },
            +[](const RunState &r) { return (r.round - 1) % 4 == 0; },
            +[](const RunState &r) { return r.enemy.hp <= r.enemy.max_hp / 3 && !r.enemy.rain_used; },
            +[](const RunState &r) { return r.you.pending.power > 0; },
            +[](const RunState &r) { return r.you.focus; },
            +[](const RunState &r) { return r.you.burn > 0; },
            +[](const RunState &r) { return r.enemy.mp <= 6; },
            +[](const RunState &r) { return !r.enemy.focus && r.enemy.last != 9; },
            +[](const RunState &) { return true; }
        };

        struct AiRule {
            int condition, skill;
        };

        const std::array<AiRule, 10> rules = {
            {{0, 17}, {1, 16}, {2, 8}, {3, 12}, {4, 11}, {5, 6}, {6, 13}, {7, 9}, {8, -1}, {8, 0}}
        };

        void reset_conditions(Fighter &f) {
            f.focus = f.guard = f.barrier = f.rest = false;
            f.burn = f.bank = 0;
            f.pending = {};
        }

        void finish_bout(RunState &r, bool won) {
            r.won = won;
            if (!won) {
                r.screen = Screen::Result;
                reset_conditions(r.you);
                reset_conditions(r.enemy);
                return;
            }
            const bool found = r.discovered && !r.registered[19];
            event(r, "VICTORY / YOU HP " + std::to_string(r.you.hp) + " / ENEMY HP 0");
            r.defeated.push_back(r.node);
            // The final audit precedes discovery registration.
            if (r.bout == 4) {
                bool legal = std::all_of(r.history.begin(), r.history.end(), [&](int id) { return r.registered[id]; });
                r.reward = legal ? RewardTier::Gold : RewardTier::Silver;
                r.screen = Screen::Result;
            } else {
                static constexpr int prizes[] = {60, 80, 100, 120};
                r.gold += prizes[r.bout];
                r.you.hp = std::min(r.you.max_hp, r.you.hp + 24);
                r.you.mp = std::min(r.you.max_mp, r.you.mp + 12);
                ++r.bout;
                r.learned_here = false;
                r.screen = (r.bout == 2 || r.bout == 3) ? Screen::Booth : Screen::Route;
            }
            reset_conditions(r.you);
            reset_conditions(r.enemy);
            if (found) {
                r.registered[19] = true;
                event(r, "You learned REPRISE: borrow a charging strike.");
                if (r.screen != Screen::Result) {
                    r.after_discovery = r.screen;
                    r.screen = Screen::Discovery;
                }
            }
        }

        void execute(EffectContext &c, Action a, bool enemy) {
            if (a.stage == Action::Rest) {
                c.user.rest = false;
                event(c.run, enemy ? "ENEMY REST" : "YOU REST");
                return;
            }
            if (a.stage == Action::Release) {
                auto p = c.user.pending;
                c.user.pending = {};
                c.user.rest = true;
                event(c.run, std::string(enemy ? "ENEMY " : "YOU ") + skills[p.skill].name + " RELEASE");
                effect_jump[0](c, {Opcode::Damage, Target::Opponent, p.power, p.magic ? 1 : 0, Phase::Hit});
                return;
            }
            if (enemy) c.user.mp -= skills[a.skill].mp;
            c.user.last = a.skill;
            if (a.skill == 8) c.user.rain_used = true;
            event(c.run, std::string(enemy ? "ENEMY used " : "YOU used ") + skills[a.skill].name);
            phase(c, Phase::Prepare);
            phase(c, Phase::Hit);
            phase(c, Phase::After);
        }

        void start_bout(RunState &r, int node) {
            r.node = node;
            r.path.push_back(node);
            auto n = r.nodes[node];
            auto d = enemies[n.enemy];
            r.enemy = Fighter{};
            r.enemy.hp = r.enemy.max_hp = d.hp;
            r.enemy.spd = d.spd;
            r.enemy.slots = n.slots;
            r.round = 1;
            r.observation = 0;
            r.discovered = false;
            r.charge_hint = r.guard_hint = false;
            r.charge_hint_round = r.guard_hint_round = 0;
            r.screen = Screen::Battle;
            r.log.clear();
            event(r, std::string(d.name) + " enters the pit.");
            r.forecast = choose_action(r);
        }
    }

    Action choose_action(const RunState &r) {
        if (r.enemy.pending.power > 0) return {r.enemy.pending.skill, Action::Release};
        if (r.enemy.rest) return {0, Action::Rest};
        for (auto rule: rules)
            if (conditions[rule.condition](r)) {
                if (rule.skill < 0) {
                    for (int id: r.enemy.slots)
                        if (direct(id) && skills[id].mp <= r.enemy.mp)
                            return
                                    {id, Action::Manual};
                } else if ((rule.skill == 0 || has(r.enemy, rule.skill)) && skills[rule.skill].mp <= r.enemy.mp)
                    return
                            {rule.skill, Action::Manual};
            }
        return {};
    }

    RunState new_run(std::uint64_t seed, bool practice) {
        RunState r;
        r.seed = seed;
        r.practice = practice;
        Lcg64 rng(seed);
        const std::array<std::array<int, 3>, 3> initial = {{{5, 3, 4}, {9, 12, 15}, {8, 14, 7}}};
        for (int i = 0; i < 3; ++i) r.you.slots[i] = initial[i][rng.percent() * 3 / 100];
        r.registered[0] = r.registered[1] = r.registered[2] = true;
        for (int id: r.you.slots) r.registered[id] = true;
        auto draw = [&](std::vector<int> &pool) {
            auto index = rng.percent() * pool.size() / 100;
            int id = pool[index];
            pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(index));
            return id;
        };
        std::vector<int> pool{0, 1, 4, 5};
        for (int i = 0; i < 2; ++i) r.nodes[i].enemy = draw(pool);
        pool.push_back(2);
        pool.push_back(3);
        std::sort(pool.begin(), pool.end());
        for (int i = 2; i < 6; ++i) r.nodes[i].enemy = draw(pool);
        pool = {6, 7};
        for (int i = 6; i < 8; ++i) r.nodes[i].enemy = draw(pool);
        r.nodes[8].enemy = 8;
        r.nodes[9].enemy = 9;
        for (auto &n: r.nodes)
            for (int s = 0; s < 3; ++s) {
                auto p = enemies[n.enemy].candidates[s];
                std::sort(p.begin(), p.end());
                n.slots[s] = p[rng.percent() * p.size() / 100];
            }
        r.rng_state = rng.state();
        return r;
    }

    bool fight(RunState &r, int id, bool next, std::string &error) {
        auto reject = [&](const char *s) {
            error = s;
            return false;
        };
        if (r.screen != Screen::Battle) return reject("戦闘画面へ戻ってください。");
        Action a{id, Action::Manual};
        if (r.you.pending.power > 0 || r.you.rest) {
            if (!next) return reject("発動・休息中です。nextで進めてください。");
            a = {
                r.you.pending.power > 0 ? r.you.pending.skill : 0,
                r.you.pending.power > 0 ? Action::Release : Action::Rest
            };
        } else {
            if (next) return reject("今は戦闘コマンドを選んでください。");
            if (id < 0 || id >= 20) return reject("その技名はありません。skillsで確認できます。");
            if (skills[id].policy == InputPolicy::EquippedOnly && !has(r.you, id)) return reject("この大技は装備していません。");
            if (r.you.mp < skills[id].mp) return reject("MPが足りません。");
            if (id == 9 && r.you.focus) return reject("既に集中しています。");
            if (id == 19 && r.enemy.pending.power <= 0) return reject("相手に発動前の予約がありません。");
        }
        Action b = r.forecast;
        EffectContext you{r, r.you, r.enemy, a.skill, 0, 0, {}, true};
        EffectContext enemy{r, r.enemy, r.you, b.skill, 0, 0, {}, false};
        bool observe = r.observation == r.round && a.stage == Action::Manual && a.skill == 0 && b.stage == Action::Rest;
        r.observation = 0;
        if (a.stage == Action::Manual) {
            r.you.mp -= skills[id].mp;
            r.history.push_back(id);
            if (r.history.size() > 6) r.history.pop_front();
            phase(you, Phase::Capture);
            phase(you, Phase::Stance);
        }
        if (b.stage == Action::Manual) phase(enemy, Phase::Stance);
        ++r.rounds;
        const bool first = r.you.spd >= r.enemy.spd;
        if (first) execute(you, a, false);
        else execute(enemy, b, true);
        if (r.you.hp > 0 && r.enemy.hp > 0) {
            if (first) execute(enemy, b, true);
            else execute(you, a, false);
        }
        if (r.you.hp > 0 && r.enemy.hp > 0) {
            if (r.you.burn > 0) {
                --r.you.burn;
                int damage = std::min(3, r.you.hp);
                r.you.hp -= damage;
                r.taken += damage;
                event(r, "YOU BURN / DAMAGE " + std::to_string(damage));
            }
            if (r.you.hp > 0 && r.enemy.burn > 0) {
                --r.enemy.burn;
                r.enemy.hp = std::max(0, r.enemy.hp - 3);
                event(r, "ENEMY BURN / DAMAGE 3");
            }
        }
        if (r.you.hp > 0 && observe) r.discovered = true;
        if (r.you.hp > 0 && a.stage == Action::Manual && a.skill == 1 && b.stage == Action::Release) {
            r.observation = r.round + 1;
            if (!r.guard_hint) {
                r.guard_hint = true;
                r.guard_hint_round = r.round + 1;
            }
        }
        r.you.guard = r.you.barrier = r.enemy.guard = r.enemy.barrier = false;
        if (r.you.hp == 0 || r.enemy.hp == 0) {
            finish_bout(r, r.you.hp > 0);
            return true;
        }
        if (r.round >= 20) {
            event(r, "TIME LIMIT / DEFEAT");
            finish_bout(r, false);
            return true;
        }
        ++r.round;
        r.forecast = choose_action(r);
        if (r.enemy.pending.power > 0 && !r.charge_hint) {
            r.charge_hint = true;
            r.charge_hint_round = r.round;
        }
        return true;
    }

    std::vector<int> routes(const RunState &r) {
        if (r.bout == 0) return {0, 1};
        if (r.bout == 1) {
            int first = r.path.front();
            return {2 + first * 2, 3 + first * 2};
        }
        if (r.bout == 2) return {6, 7};
        return {r.bout == 3 ? 8 : 9};
    }

    std::vector<int> learnable(const RunState &r) {
        std::array<bool, 20> candidate{};
        for (int n: r.defeated) for (int id: r.nodes[n].slots) if (id >= 3 && id < 19) candidate[id] = true;
        if (r.bout == 2) candidate[10] = true;
        if (r.bout == 3) candidate[19] = true;
        std::vector<int> result;
        for (int id = 3; id < 20; ++id) if (candidate[id] && !r.registered[id]) result.push_back(id);
        return result;
    }

    std::array<int, 3> stats(int armor, int charm) {
        std::array<int, 3> result{96, 32, 100};
        for (int id: {armor, charm})
            if (id >= 0 && id < 10)
                for (int s = 0; s < 3; ++s) {
                    auto t = items[id].stats[s];
                    result[s] = static_cast<int>(static_cast<std::int64_t>(result[s]) * t.mul / t.div + t.add);
                }
        for (int s = 0; s < 3; ++s) result[s] = std::clamp(result[s], s == 0 ? 1 : 0, 999);
        return result;
    }

    bool operation(RunState &r, const std::string &op, std::string &error) {
        std::istringstream in(op);
        std::string verb, extra;
        int id = -99, slot = -99;
        in >> verb;
        auto end = [&] { return !(in >> extra); };
        auto reject = [&] {
            error = "この画面では受理できない操作です。";
            return false;
        };
        if (verb == "use") {
            std::string name;
            if (!(in >> name) || !end()) return reject();
            return fight(r, skill_id(name), false, error);
        }
        if (verb == "next") {
            if (!end()) return reject();
            return fight(r, 0, true, error);
        }
        if (verb == "route") {
            if (!(in >> id) || !end() || r.screen != Screen::Route) return reject();
            auto choices = routes(r);
            if (std::find(choices.begin(), choices.end(), id) == choices.end()) return reject();
            start_bout(r, id);
            return true;
        }
        if (verb == "learn" || verb == "equip-skill") {
            if (!(in >> id >> slot) || !end() || id < 3 || id >= 20 || slot < 1 || slot > 3) return reject();
            if (r.screen == Screen::Discovery) {
                if (verb != "equip-skill" || id != 19) return reject();
                r.you.slots[slot - 1] = id;
                r.screen = r.after_discovery;
                return true;
            }
            if (r.screen != Screen::Booth) return reject();
            if (verb == "learn") {
                auto choices = learnable(r);
                if (r.learned_here || std::find(choices.begin(), choices.end(), id) == choices.end()) return reject();
                r.registered[id] = true;
                r.learned_here = true;
            } else if (!r.registered[id]) return reject();
            r.you.slots[slot - 1] = id;
            return true;
        }
        if (verb == "leave-booth") {
            if (!end()) return reject();
            if (r.screen == Screen::Discovery) {
                r.screen = r.after_discovery;
                return true;
            }
            if (r.screen != Screen::Booth) return reject();
            r.screen = Screen::Shop;
            r.preview_armor = r.armor;
            r.preview_charm = r.charm;
            return true;
        }
        if (verb == "buy" || verb == "equip-item") {
            if (!(in >> id) || !end() || r.screen != Screen::Shop || id < -2 || id >= 10) return reject();
            if (verb == "buy") {
                if (id < 0 || r.owned[id] || (items[id].second && r.bout != 3)) return reject();
                if (r.gold < items[id].price) {
                    error = "Goldが足りません。";
                    return false;
                }
                r.gold -= items[id].price;
                r.owned[id] = true;
            } else if (id >= 0 && !r.owned[id]) return reject();
            if (id == -1) r.preview_armor = -1;
            else if (id == -2) r.preview_charm = -1;
            else if (items[id].charm) r.preview_charm = id;
            else r.preview_armor = id;
            return true;
        }
        if (verb == "leave-shop") {
            if (!end() || r.screen != Screen::Shop) return reject();
            auto s = stats(r.preview_armor, r.preview_charm);
            r.you.hp = std::min(s[0], r.you.hp + std::max(0, s[0] - r.you.max_hp));
            r.you.mp = std::min(s[1], r.you.mp + std::max(0, s[1] - r.you.max_mp));
            r.you.max_hp = s[0];
            r.you.max_mp = s[1];
            r.you.spd = s[2];
            r.armor = r.preview_armor;
            r.charm = r.preview_charm;
            r.screen = Screen::Route;
            return true;
        }
        return reject();
    }

    int score(const RunState &r) { return std::max(0, 10000 - 120 * r.rounds - 10 * r.taken + 20 * r.maximum_damage); }
}
