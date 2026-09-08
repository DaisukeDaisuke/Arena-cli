#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace arena {
    enum class Opcode { Damage, Heal, Apply, Clear, RestoreMP, ConditionalAdd, Drain, SpendBank, Schedule, Borrow };

    enum class Target { Self, Opponent };

    enum class Phase { Prepare, Hit, After, Stance, Capture };

    enum class InputPolicy { GlobalName, EquippedOnly };

    struct Effect {
        Opcode op;
        Target target;
        int arg0 = 0, arg1 = 0;
        Phase phase = Phase::After;
    };

    struct SkillDef {
        const char *name;
        const char *japanese;
        const char *description;
        int mp;
        InputPolicy policy;
        std::vector<Effect> effects;
    };

    struct Pending {
        int skill = -1, power = 0;
        bool magic = false;
    };

    struct Fighter {
        int hp = 96, max_hp = 96, mp = 32, max_mp = 32, spd = 100;
        bool focus = false, guard = false, barrier = false, rest = false, rain_used = false;
        int burn = 0, bank = 0, last = -1;
        Pending pending;
        std::array<int, 3> slots{5, 9, 7};
    };

    struct EnemyDef {
        const char *name;
        int hp, spd, art;
        std::array<std::vector<int>, 3> candidates;
    };

    struct Transform {
        int mul = 1, div = 1, add = 0;
    };

    struct ItemDef {
        const char *name;
        const char *japanese;
        const char *description;
        int price;
        bool charm, second;
        std::array<Transform, 3> stats;
    };

    struct Node {
        int enemy = 0;
        std::array<int, 3> slots{};
    };

    struct Action {
        int skill = 0;

        enum Stage { Manual, Release, Rest } stage = Manual;
    };

    enum class Screen { Route, Battle, Booth, Shop, Discovery, Result, Closed };

    enum class RewardTier { None, Silver, Gold };

    struct RunState {
        std::uint64_t seed = 0, rng_state = 0;
        bool practice = false;
        Fighter you, enemy;
        std::array<Node, 10> nodes{};
        std::array<bool, 20> registered{};
        std::array<bool, 10> owned{};
        std::vector<int> defeated, path;
        std::deque<int> history;
        std::deque<std::string> log;
        int bout = 0, node = -1, round = 1, gold = 0, armor = -1, charm = -1, preview_armor = -1, preview_charm = -1;
        int rounds = 0, taken = 0, maximum_damage = 0, observation = 0;
        bool discovered = false, learned_here = false, charge_hint = false, guard_hint = false;
        int charge_hint_round = 0, guard_hint_round = 0;
        Screen screen = Screen::Route, after_discovery = Screen::Route;
        RewardTier reward = RewardTier::None;
        bool won = false;
        Action forecast;
    };

    struct EffectContext {
        RunState &run;
        Fighter &user;
        Fighter &opponent;
        int skill;
        int add = 0, damage = 0;
        Pending captured;
        bool player = false;
    };

    using EffectFn = void (*)(EffectContext &, const Effect &);

    EffectFn damage_handler();

    EffectFn heal_handler();

    EffectFn apply_handler();

    EffectFn clear_handler();

    EffectFn restore_mp_handler();

    EffectFn conditional_add_handler();

    EffectFn drain_handler();

    EffectFn spend_bank_handler();

    EffectFn schedule_handler();

    EffectFn borrow_handler();

    extern const std::array<SkillDef, 20> skills;
    extern const std::array<EnemyDef, 10> enemies;
    extern const std::array<ItemDef, 10> items;
    extern const std::array<std::array<const char *, 16>, 6> art;
    extern const std::array<std::array<unsigned, 6>, 13> palettes;
    extern const std::array<EffectFn, 10> effect_jump;

    void event(RunState &, const std::string &);

    RunState new_run(std::uint64_t, bool);

    Action choose_action(const RunState &);

    bool fight(RunState &, int skill, bool next, std::string &error);

    std::vector<int> routes(const RunState &);

    std::vector<int> learnable(const RunState &);

    std::array<int, 3> stats(int armor, int charm);

    bool operation(RunState &, const std::string &, std::string &error);

    int skill_id(const std::string &);

    int score(const RunState &);

    bool validate_data(std::string &);

    int self_test();

    struct Options {
        enum Color { Plain, Rgb, Osc } color = Rgb;

        bool clear = false, progress = false;
    };

    enum class View { Main, Help, Skills, Book, Status, Recent, Completion, EquipSkills, Slot, Purchase, Quit };

    struct Presentation {
        View view = View::Main;
        std::string message;
        std::string prefix;
        int selected = -1;
        bool learning = false;
        bool result_final = false;
    };

    class Renderer {
        Options options;
        bool drawn = false;
        std::array<bool, 42> used{};

        std::string ink(int index) const;

        std::string normal() const;

        std::string row(const std::string &) const;

        std::string gauge(int, int, int) const;

        std::vector<std::string> sprite(int character);

        std::string heading(const std::string &title) const;
        std::string border() const;
        std::string status(const RunState &) const;
        std::string route_map(const RunState &) const;
        std::string opponents(const RunState &) const;
        std::string skill_description(const RunState &, int id) const;
        std::string battle_screen(const RunState &);
        std::string shop_screen(const RunState &) const;
        std::string result_screen(const RunState &, bool final);
        std::string main_screen(const RunState &, bool final);
        std::string information(const RunState &, const Presentation &) const;

    public:
        explicit Renderer(Options o) : options(o) {
        }

        std::string render(const RunState &, const Presentation &);

        std::string finish();

        std::string progress(int bout) const;
    };

    std::string help_text();

    std::string seed_text(std::uint64_t);
    std::string normalize(std::string);
}
