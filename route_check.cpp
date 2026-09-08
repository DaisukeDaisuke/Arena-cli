#include "game.h"
#include <iostream>
#include <optional>
#include <set>

namespace {
    using arena::RunState;

    std::array<int, 24> combat_key(const RunState &run) {
        const auto &you = run.you;
        const auto &enemy = run.enemy;
        return {you.hp, you.mp, you.focus, you.burn, you.bank, you.rest,
                you.pending.skill, you.pending.power, you.pending.magic,
                enemy.hp, enemy.mp, enemy.focus, enemy.burn, enemy.bank, enemy.rest,
                enemy.pending.skill, enemy.pending.power, enemy.pending.magic,
                enemy.last, enemy.rain_used, run.observation, run.discovered,
                run.forecast.skill, run.forecast.stage};
    }

    int position_value(const RunState &run) {
        return run.you.hp * 4 + run.you.mp + (run.enemy.max_hp - run.enemy.hp) * 4 +
               (run.you.focus ? 8 : 0) + run.you.bank - run.you.burn * 3;
    }

    bool run_command(RunState &run, const std::string &command) {
        std::string error;
        return arena::operation(run, command, error);
    }

    std::optional<RunState> win_bout(const RunState &start) {
        std::vector<RunState> frontier{start};
        for (int round = start.round; round <= 20; ++round) {
            std::vector<RunState> next;
            std::optional<RunState> winner;
            for (const auto &position : frontier) {
                std::vector<int> choices{0, 1, 2};
                if (position.you.pending.power > 0 || position.you.rest) {
                    choices = {-1};
                } else {
                    for (int id : position.you.slots) {
                        // The acceptance search never uses hidden names or reprise.
                        if (id != 19 && position.registered[id] &&
                            std::find(choices.begin(), choices.end(), id) == choices.end()) {
                            choices.push_back(id);
                        }
                    }
                }
                for (int id : choices) {
                    auto candidate = position;
                    const std::string command = id < 0 ? "next" : std::string("use ") + arena::skills[id].name;
                    if (!run_command(candidate, command)) {
                        continue;
                    }
                    if (candidate.screen != arena::Screen::Battle) {
                        if (candidate.won && (!winner || position_value(candidate) > position_value(*winner))) {
                            winner = std::move(candidate);
                        }
                    } else {
                        next.push_back(std::move(candidate));
                    }
                }
            }
            if (winner) {
                return winner;
            }
            std::stable_sort(next.begin(), next.end(), [](const RunState &left, const RunState &right) {
                return position_value(left) > position_value(right);
            });
            frontier.clear();
            std::set<std::array<int, 24>> seen;
            for (auto &candidate : next) {
                if (seen.insert(combat_key(candidate)).second) {
                    frontier.push_back(std::move(candidate));
                }
                if (frontier.size() == 32) {
                    break;
                }
            }
            if (frontier.empty()) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    bool prepare_next(RunState &run) {
        if (run.screen == arena::Screen::Discovery && !run_command(run, "leave-booth")) {
            return false;
        }
        if (run.screen == arena::Screen::Booth) {
            if (!run_command(run, "leave-booth")) {
                return false;
            }
            // Both shops use legal purchases within the specified 240 G total budget.
            if (!run_command(run, run.bout == 2 ? "buy 1" : "buy 5") || !run_command(run, "leave-shop")) {
                return false;
            }
        }
        return true;
    }

    std::optional<RunState> next_bout(RunState run, int choice) {
        if (!prepare_next(run)) {
            return std::nullopt;
        }
        const auto routes = arena::routes(run);
        if (!run_command(run, "route " + std::to_string(routes.at(choice)))) {
            return std::nullopt;
        }
        return win_bout(run);
    }

    bool check_configuration(const RunState &initial, int &completed) {
        const auto seed = initial.seed;
        for (int first = 0; first < 2; ++first) {
            const auto bout_one = next_bout(initial, first);
            if (!bout_one) {
                std::cerr << "No witness: seed " << seed << " bout1 " << first << '\n';
                return false;
            }
            for (int second = 0; second < 2; ++second) {
                const auto bout_two = next_bout(*bout_one, second);
                if (!bout_two) {
                    std::cerr << "No witness: seed " << seed << " path " << first << second << " bout2\n";
                    return false;
                }
                for (int third = 0; third < 2; ++third) {
                    auto state = next_bout(*bout_two, third);
                    if (state) state = next_bout(*state, 0);
                    if (state) state = next_bout(*state, 0);
                    if (!state || !state->won || state->reward != arena::RewardTier::Gold) {
                        std::cerr << "No witness: seed " << seed << " path " << first << second << third << " later bouts\n";
                        return false;
                    }
                    ++completed;
                }
            }
        }
        return true;
    }
}

int main() {
    int completed = 0;
    for (std::uint64_t seed = 0; seed < 1000; ++seed) {
        if (!check_configuration(arena::new_run(seed, true), completed)) {
            return 1;
        }
    }
    if (!check_configuration(arena::new_run(UINT64_MAX, true), completed)) {
        return 1;
    }
    int fixtures = 0;
    for (int attack : {5, 3, 4}) {
        for (int support : {9, 12, 15}) {
            for (int healing : {8, 14, 7}) {
                auto initial = arena::new_run(1, true);
                initial.you.slots = {attack, support, healing};
                initial.registered.fill(false);
                for (int id : {0, 1, 2, attack, support, healing}) {
                    initial.registered[id] = true;
                }
                if (!check_configuration(initial, fixtures)) {
                    std::cerr << "Initial-slot fixture: " << attack << ',' << support << ',' << healing << '\n';
                    return 1;
                }
            }
        }
    }
    std::cout << "PASS " << completed << " ordinary championship witnesses: seeds 0..999 and UINT64_MAX, all 8 routes\n"
              << "PASS " << fixtures << " explicit initial-slot fixtures: all 27 combinations, all 8 routes\n"
              << "Registered equipped skills and legal purchases only; no reprise.\n"
              << "Finite beam search witnesses are not a proof for all 2^64 seeds.\n";
    return 0;
}
