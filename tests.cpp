#include "game.h"
#include "lcg.hpp"
#include "session.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace arena {
    namespace {
        void require(bool condition, const char *message) {
            if (!condition) {
                throw std::runtime_error(message);
            }
        }

        void use(RunState &run, const std::string &command) {
            std::string error;
            require(operation(run, command, error), (command + ": " + error).c_str());
        }

        RunState final_fixture() {
            auto run = new_run(1, true);
            run.bout = 4;
            use(run, "route 9");
            run.you.slots = {9, 10, 19};
            run.registered[9] = true;
            run.registered[10] = true;
            return run;
        }

        void random_tests() {
            Lcg64 generator(1);
            constexpr std::array<std::uint64_t, 5> vector{
                UINT64_C(0x5d588b656c2e2828), UINT64_C(0xe3b543e45af1de8b),
                UINT64_C(0x3d7fca1a0b78ce9a), UINT64_C(0x3fe714a2cb968b85),
                UINT64_C(0x756b99a0cfd8d73c)
            };
            for (auto value : vector) {
                require(generator.next() == value, "LCG update vector");
            }
            Lcg64 percent(1);
            for (auto value : {36u, 88u, 24u, 24u, 45u}) {
                require(percent.percent() == value, "LCG percentage vector");
            }
            const auto run = new_run(1, true);
            require(run.you.slots == std::array<int, 3>{3, 15, 8}, "Initial seed 1 slots");
            require(49 * 2 / 100 == 0 && 50 * 2 / 100 == 1, "Two-way boundary");
            require(33 * 3 / 100 == 0 && 34 * 3 / 100 == 1 &&
                    66 * 3 / 100 == 1 && 67 * 3 / 100 == 2, "Three-way boundary");
            for (std::uint64_t seed = 0; seed < 1000; ++seed) {
                Lcg64 expected(seed);
                for (int count = 0; count < 41; ++count) {
                    require(expected.percent() < 100, "Percentage range");
                }
                const auto candidate = new_run(seed, true);
                require(candidate.rng_state == expected.state(), "Exactly 41 draws");
                std::array<bool, 10> present{};
                for (const auto &node : candidate.nodes) {
                    require(!present[node.enemy], "Unique enemy placement");
                    present[node.enemy] = true;
                    for (int slot = 0; slot < 3; ++slot) {
                        const auto &pool = enemies[node.enemy].candidates[slot];
                        require(std::find(pool.begin(), pool.end(), node.slots[slot]) != pool.end(), "Enemy slot pool");
                    }
                }
            }
            for (auto seed : {UINT64_C(0), UINT64_MAX}) {
                const auto candidate = new_run(seed, true);
                Lcg64 expected(seed);
                for (int draw = 0; draw < 41; ++draw) expected.next();
                require(candidate.rng_state == expected.state(), "Extreme seed");
            }
        }

        void defense_tests() {
            auto guarded = final_fixture();
            use(guarded, "use wait");
            use(guarded, "use guard");
            require(guarded.you.hp == 72 && guarded.you.bank == 12, "Guard 48 -> 24 / Bank12");

            auto barrier = final_fixture();
            use(barrier, "use wait");
            use(barrier, "use barrier");
            require(barrier.you.hp == 96 && barrier.you.bank == 0, "Barrier blocks magic without Bank");

            auto slow = final_fixture();
            slow.you.spd = 0;
            use(slow, "use attack");
            require(slow.enemy.hp == 72, "SPD0 still acts");
            use(slow, "use guard");
            require(slow.you.hp == 72 && slow.you.bank == 12, "Slow guard is a pre-round stance");

            RunState damage;
            damage.enemy.hp = 100;
            damage.enemy.max_hp = 100;
            damage.enemy.guard = true;
            EffectContext context{damage, damage.you, damage.enemy, 3, 0, 0, {}, true};
            damage_handler()(context, {Opcode::Damage, Target::Opponent, 14, 2, Phase::Hit});
            require(damage.enemy.hp == 86 && damage.enemy.bank == 0, "Pierce ignores Guard");
            damage.enemy.barrier = true;
            damage.you.focus = true;
            damage_handler()(context, {Opcode::Damage, Target::Opponent, 14, 3, Phase::Hit});
            require(damage.enemy.hp == 86 && !damage.you.focus && damage.enemy.bank == 0, "Barrier priority / zero consumes Focus");
        }

        void reservation_tests() {
            auto run = final_fixture();
            std::string error;
            const auto initial_mp = run.you.mp;
            require(!operation(run, "use eclipse", error), "Unowned direct ultimate is rejected");
            require(!operation(run, "use reprise", error), "Reprise needs an existing reservation");
            require(run.you.mp == initial_mp && run.history.empty() && run.round == 1, "Rejected actions are free");
            use(run, "use wait");
            require(run.enemy.pending.power == 48 && run.enemy.mp == 24, "Enemy charge cost once");
            use(run, "use guard");
            use(run, "use attack");
            use(run, "use focus");
            use(run, "use reprise");
            require(run.you.pending.power == 60 && run.you.bank == 0 && run.you.focus, "Borrow adds Bank / preserves Focus");
            require(run.you.hp == 24 && run.you.mp == 18, "Borrow round damage and cost");
            const auto history = run.history;
            use(run, "next");
            require(run.won && run.maximum_damage == 72 && run.reward == RewardTier::Silver, "90 power / 72 actual / final audit first");
            require(run.history == history && run.registered[19], "Release is not history / discovery after audit");

            auto equipped = final_fixture();
            equipped.you.slots[0] = 17;
            equipped.registered[17] = true;
            use(equipped, "use eclipse");
            const auto cost = equipped.you.mp;
            use(equipped, "next");
            require(equipped.you.rest && equipped.you.mp == cost, "Own release -> rest, no extra MP");
            require(!operation(equipped, "use attack", error), "Rest refuses manual actions");
            use(equipped, "next");
            require(!equipped.you.rest && equipped.history.size() == 1, "Rest consumed without history");

            auto slower = final_fixture();
            use(slower, "use wait");
            slower.you.spd = 0;
            use(slower, "use reprise");
            require(slower.you.pending.power == 48, "Borrow captures before faster opponent releases");
        }

        void history_and_victory_tests() {
            auto run = final_fixture();
            for (const auto *name : {"focus", "guard", "attack", "attack", "guard", "attack",
                                     "focus", "guard", "attack", "attack", "attack"}) {
                use(run, std::string("use ") + name);
            }
            require(run.won && run.rounds == 11 && run.you.hp == 24, "Known ordinary final victory");
            require(run.reward == RewardTier::Gold, "Ordinary registered commands earn Gold");

            auto history = final_fixture();
            history.enemy.mp = 0;
            history.enemy.max_hp = history.enemy.hp = 999;
            history.forecast = choose_action(history);
            require(!history.registered[5], "Fixture fire unregistered");
            use(history, "use fire");
            for (int count = 0; count < 5; ++count) use(history, "use guard");
            require(history.history.front() == 5, "Illegal command survives five commands");
            use(history, "use guard");
            require(std::find(history.history.begin(), history.history.end(), 5) == history.history.end(), "Six commands evict oldest");

            auto killed = final_fixture();
            killed.enemy.hp = 12;
            killed.enemy.pending = {17, 48, true};
            killed.forecast = choose_action(killed);
            use(killed, "use attack");
            require(killed.won && killed.you.hp == 96 && killed.enemy.pending.power == 0, "First kill cancels release");

            auto timeout = final_fixture();
            timeout.enemy.mp = 0;
            timeout.enemy.hp = timeout.enemy.max_hp = 999;
            timeout.you.hp = timeout.you.max_hp = 999;
            timeout.forecast = choose_action(timeout);
            for (int count = 0; count < 20; ++count) use(timeout, "use guard");
            require(timeout.screen == Screen::Result && !timeout.won && timeout.rounds == 20, "20-round timeout");
        }

        void effects_tests() {
            auto rain = final_fixture();
            rain.you.hp = 60;
            rain.enemy.hp = 60;
            use(rain, "use healing-rain");
            require(rain.you.hp == 72 && rain.enemy.hp == 66, "Rain heals 12/6");
            use(rain, "use healing-rain");
            require(rain.you.hp == 36 && rain.enemy.hp == 72, "Player can repeat rain");

            auto drain = final_fixture();
            drain.you.hp = 40;
            drain.enemy.hp = 5;
            use(drain, "use drain");
            require(drain.won && drain.you.hp == 42 && drain.maximum_damage == 5, "Lethal drain heals actual loss before victory");

            auto burns = final_fixture();
            burns.you.hp = 3;
            burns.you.burn = 1;
            burns.enemy.hp = 3;
            burns.enemy.burn = 1;
            use(burns, "use wait");
            require(!burns.won && burns.you.hp == 0 && burns.enemy.hp == 3, "Player Burn death cancels enemy Burn");

            auto purge = final_fixture();
            purge.you.focus = true;
            purge.you.spd = 0;
            purge.enemy.pending = {};
            purge.enemy.slots = {11, 3, 13};
            purge.forecast = choose_action(purge);
            use(purge, "use attack");
            require(purge.enemy.hp == 72 && !purge.you.focus, "Faster purge removes Focus before attack");

            auto counter = final_fixture();
            use(counter, "use wait");
            use(counter, "use counter");
            require(counter.enemy.hp == 56, "Counter before release gets bonus");
            auto late_counter = final_fixture();
            use(late_counter, "use wait");
            late_counter.you.spd = 0;
            use(late_counter, "use counter");
            require(late_counter.enemy.hp == 74, "Counter after release has no bonus");

            auto fire = final_fixture();
            use(fire, "use fire");
            require(fire.enemy.hp == 69 && fire.enemy.burn == 1, "Burn includes application round");
            use(fire, "use flare");
            require(fire.enemy.hp == 40 && fire.enemy.burn == 0, "Flare adds 14 before final Burn tick");

            auto focus = final_fixture();
            use(focus, "use focus");
            const int mp = focus.you.mp;
            std::string error;
            require(!operation(focus, "use focus", error) && focus.you.mp == mp, "Duplicate Focus is non-consuming");
            use(focus, "use feint");
            require(focus.enemy.hp == 72 && focus.you.focus, "Feint spends and renews Focus");

            auto bank = final_fixture();
            bank.you.bank = 12;
            use(bank, "use bank-shot");
            require(bank.enemy.hp == 58 && bank.you.bank == 0, "Bank-shot spends saved power");
            bank.you.mp = 1;
            const auto round = bank.round;
            require(!operation(bank, "use bank-shot", error) && bank.round == round && bank.you.mp == 1,
                    "Insufficient MP does not advance state");

            auto mend = final_fixture();
            mend.you.hp = 60;
            mend.you.burn = 2;
            use(mend, "use mend");
            require(mend.you.hp == 72 && mend.you.burn == 0, "Mend heals and clears Burn before tick");
            const auto before_siphon = mend.you.mp;
            use(mend, "use siphon");
            require(mend.you.mp == std::min(mend.you.max_mp, before_siphon + 3), "Siphon pays two and restores five");

            auto enemy_rain = final_fixture();
            enemy_rain.enemy.slots = {8, 3, 13};
            enemy_rain.enemy.hp = 10;
            enemy_rain.forecast = choose_action(enemy_rain);
            require(enemy_rain.forecast.skill == 8, "Low HP enemy chooses rain");
            use(enemy_rain, "use wait");
            require(enemy_rain.enemy.hp == 22 && enemy_rain.enemy.rain_used, "Enemy rain is marked used");
            enemy_rain.enemy.hp = 10;
            require(choose_action(enemy_rain).skill != 8, "Enemy AI cannot repeat rain");
        }

        void shop_tests() {
            require(stats(1, 3) == std::array<int, 3>{196, 32, 0}, "Zero times two stays zero");
            require(stats(1, 5) == std::array<int, 3>{196, 32, 100}, "Zero plus 100");
            require(stats(0, 8) == std::array<int, 3>{152, 28, 80}, "Iron / heart");
            require(stats(0, 3) == std::array<int, 3>{128, 32, 160}, "Iron / wings");
            require(stats(2, 4) == std::array<int, 3>{108, 60, 100}, "Pilgrim / mana");
            require(stats(6, 8) == std::array<int, 3>{104, 28, 150}, "Duelist / heart");
            require(stats(7, 9) == std::array<int, 3>{54, 88, 120}, "Reservoir / prism");
            auto run = new_run(1, true);
            run.bout = 2;
            run.screen = Screen::Shop;
            run.gold = 140;
            run.you.hp = 50;
            std::string error;
            require(!operation(run, "equip-item 1", error), "Unowned equipment rejected");
            require(!operation(run, "buy 5", error), "Shop I cannot buy quick pin");
            require(!operation(run, "buy -1", error), "Negative item purchase rejected");
            use(run, "buy 0");
            require(run.gold == 60 && run.you.hp == 50 && run.you.max_hp == 96, "Purchase only changes preview");
            require(!operation(run, "buy 0", error), "No duplicate purchase");
            require(!operation(run, "buy 1", error) && run.gold == 60, "Price is checked");
            for (int count = 0; count < 6; ++count) {
                use(run, "equip-item -1");
                use(run, "equip-item 0");
            }
            use(run, "leave-shop");
            require(run.you.hp == 82 && run.you.max_hp == 128, "Equipment healing applied once from entry state");
            require(!operation(run, "buy 4", error), "No purchase outside shop");
        }

        void input_and_replay_tests() {
            Session session(1, true);
            session.accept(" 1 ");
            const auto forecast = session.run.forecast;
            const auto mp = session.run.you.mp;
            for (const auto *command : {"help", "fire", "back", "book", "back", "? fi", "back", "unknown"}) {
                session.accept(command);
            }
            require(session.operations.size() == 1 && session.run.you.mp == mp && session.run.round == 1, "Information and invalid input consume nothing");
            require(session.run.forecast.skill == forecast.skill && session.run.forecast.stage == forecast.stage, "Forecast is immutable during input");
            session.accept(" WAIT ");
            std::ostringstream saved;
            write_record(saved, session);
            Session restored(0, false);
            std::istringstream input(saved.str());
            std::string error;
            require(resume_session(input, restored, error), "Resume valid transcript");
            require(restored.operations == session.operations && restored.run.you.hp == session.run.you.hp &&
                    restored.run.enemy.hp == session.run.enemy.hp && restored.run.round == session.run.round &&
                    restored.run.rng_state == session.run.rng_state, "Replay reconstructs combat");
            auto corrupted = saved.str();
            corrupted.insert(corrupted.find("END "), "buy 1\n");
            std::istringstream invalid(corrupted);
            require(!resume_session(invalid, restored, error), "Replay uses ordinary purchase validator");
            std::istringstream truncated(saved.str().substr(0, saved.str().find("END ")));
            require(!resume_session(truncated, restored, error), "Truncated transcript rejected");
            std::istringstream long_line(std::string(10000, 'a') + "\nwait\n");
            std::string line;
            require(read_line(long_line, line) == LineResult::TooLong, "Long input discarded");
            require(read_line(long_line, line) == LineResult::Line && line == "wait", "Next input is intact");
            require(read_line(long_line, line) == LineResult::End, "EOF terminates input");
            session.accept("quit");
            session.accept("");
            require(!session.closed && session.display.view == View::Main, "Blank cancels quit");

            Session shop(1, true);
            shop.run.screen = Screen::Shop;
            shop.run.bout = 2;
            shop.run.gold = 140;
            shop.accept("2");
            shop.accept("help");
            shop.accept("back");
            require(shop.display.view == View::Purchase && shop.display.selected == 1, "Information preserves purchase selection");
            shop.accept("2");
            require(shop.operations.empty() && shop.run.gold == 140, "Cancelled purchase leaves no operation");
            shop.accept("2");
            shop.accept("1");
            require(shop.run.gold == 0 && shop.run.owned[1] && shop.operations.back() == "buy 1",
                    "Confirmed purchase is normalized");
        }

        void full_run_replay_tests() {
            Session session(1, true);
            auto record = [&](const std::string &command) {
                std::string error;
                require(session.replay(command, error), (command + ": " + error).c_str());
                std::ostringstream saved;
                write_record(saved, session);
                Session restored(0, false);
                std::istringstream input(saved.str());
                require(resume_session(input, restored, error), "Every committed state can resume");
                Renderer expected({Options::Plain, false, false});
                Renderer actual({Options::Plain, false, false});
                require(expected.render(session.run, {}) == actual.render(restored.run, {}),
                        "Replay reproduces screen, stats, hints and logs");
                require(session.run.registered == restored.run.registered && session.run.owned == restored.run.owned &&
                        session.run.history == restored.run.history, "Replay reproduces ownership and history");
            };
            for (int bout = 0; bout < 5; ++bout) {
                if (session.run.screen == Screen::Discovery) record("leave-booth");
                if (session.run.screen == Screen::Booth) {
                    const int lesson = bout == 2 ? 10 : 19;
                    if (!session.run.registered[lesson]) {
                        record("learn " + std::to_string(lesson) + " 2");
                    }
                    record("equip-skill 15 2");
                    record("leave-booth");
                    record(bout == 2 ? "buy 1" : "buy 5");
                    record("equip-item -1");
                    record("equip-item 1");
                    record("leave-shop");
                }
                record("route " + std::to_string(routes(session.run).front()));
                while (session.run.screen == Screen::Battle) {
                    record(session.run.forecast.stage == Action::Release ? "use guard" : "use attack");
                }
                require(session.run.won, "Replay fixture wins every bout");
            }
            require(session.run.reward == RewardTier::Gold, "Complete replay fixture wins gold");
        }

        void rendering_tests() {
            auto run = final_fixture();
            Presentation display;
            Renderer plain({Options::Plain, false, false});
            const auto first = plain.render(run, display);
            const auto second = plain.render(run, display);
            require(first.find('\x1b') == std::string::npos && plain.finish().empty(), "Plain has no terminal escapes");
            require(first.find("[##############] 96/96") != std::string::npos, "14-cell full HP gauge");
            require(first.find("NEXT: ECLIPSE / CHARGE") != std::string::npos, "Forecast in main UI");
            require(second.compare(0, 8, std::string(8, '\n')) == 0, "Append separator is eight LF");
            require(first.front() != '\n', "First render has no separator");
            auto check_frame_width = [](const std::string &screen) {
                std::istringstream input(screen);
                std::string line;
                while (std::getline(input, line)) {
                    if (!line.empty() && (line.front() == '|' || line.front() == '+')) {
                        require(line.size() == 76, "Every plain frame row has exactly 76 columns");
                    }
                }
            };
            check_frame_width(first);
            run.you.slots = {8, 8, 8};
            check_frame_width(plain.render(run, display));
            Renderer clear({Options::Rgb, true, false});
            const auto cleared = clear.render(run, display);
            require(cleared.compare(0, 7, "\x1b[2J\x1b[H") == 0, "Clear is 2J then H");
            require(cleared.find("\x1b]") == std::string::npos, "RGB emits no OSC");
            Renderer osc({Options::Osc, false, true});
            const auto indexed = osc.render(run, display);
            require(indexed.find("\x1b]4;18;rgb:51/41/74\a") != std::string::npos, "Knight palette");
            require(osc.progress(5) == "\x1b]9;4;1;100\a", "Progress 100 percent");
            const auto cleanup = osc.finish();
            require(cleanup.find("\x1b]104;16\a") != std::string::npos && cleanup.find("\x1b]104;41\a") != std::string::npos,
                    "Only used palette indices reset");
            require(cleanup.find("\x1b]104\a") == std::string::npos, "Never reset all palettes");
            require(run.round == 1 && run.history.empty(), "Rendering is read-only");
            for (int enemy = 0; enemy < 10; ++enemy) {
                run.nodes[9].enemy = enemy;
                const auto drawing = plain.render(run, display);
                require(!drawing.empty(), "All enemy art is renderable");
                check_frame_width(drawing);
            }
            auto route = new_run(1, true);
            check_frame_width(plain.render(route, display));
            display.view = View::Status;
            route.you.hp = 100;
            route.you.max_hp = 128;
            route.you.mp = 20;
            route.you.bank = 6;
            const auto status = plain.render(route, display);
            require(status.find("[##########----] 100/128") != std::string::npos, "HP gauge floors 100/128");
            require(status.find("[########------] 20/32") != std::string::npos, "MP gauge floors 20/32");
            require(status.find("[#######-------] 6/12") != std::string::npos, "Bank gauge floors 6/12");
            check_frame_width(status);
        }
    }

    int self_test() {
        const std::array<std::pair<const char *, void (*)()>, 9> suites{{
            {"LCG / placement", random_tests},
            {"Guard / Barrier / speed", defense_tests},
            {"Reservation / reprise", reservation_tests},
            {"History / ordinary victory", history_and_victory_tests},
            {"Effects / death order", effects_tests},
            {"Shop / equipment", shop_tests},
            {"Input / replay", input_and_replay_tests},
            {"Full tournament replay", full_run_replay_tests},
            {"Terminal rendering", rendering_tests}
        }};
        int failed = 0;
        for (const auto &suite : suites) {
            try {
                suite.second();
                std::cout << "PASS " << suite.first << '\n';
            } catch (const std::exception &exception) {
                ++failed;
                std::cerr << "FAIL " << suite.first << ": " << exception.what() << '\n';
            }
        }
        std::cout << suites.size() - failed << '/' << suites.size() << " suites passed\n";
        return failed == 0 ? 0 : 1;
    }
}
