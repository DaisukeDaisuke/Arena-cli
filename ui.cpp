#include "game.h"
#include "session.h"
#include <iomanip>
#include <sstream>

namespace arena {
    namespace {
        constexpr std::array<unsigned, 8> ui_colors{
            0x394B65, 0x66D19E, 0x70AFFF, 0x67DDE8,
            0xFFC857, 0xC49BFF, 0xFF637D, 0x7F8BA3
        };
        constexpr const char *reset = "\x1b[0m";
        constexpr const char *title = R"(       /\                         /\
  ____/  \____  V O I D  P I T  __/  \____
  \          /  BORROWED CROWN  \        /
   \___  ___/====================\__  __/
       \/                            \/
)";

        std::string rgb(unsigned color, bool background = false) {
            return std::string("\x1b[") + (background ? "48" : "38") + ";2;" +
                   std::to_string((color >> 16) & 255) + ";" +
                   std::to_string((color >> 8) & 255) + ";" +
                   std::to_string(color & 255) + "m";
        }

        std::string define_palette(int index, unsigned color) {
            std::ostringstream output;
            output << "\x1b]4;" << index << ";rgb:" << std::hex << std::setfill('0')
                   << std::setw(2) << ((color >> 16) & 255) << '/'
                   << std::setw(2) << ((color >> 8) & 255) << '/'
                   << std::setw(2) << (color & 255) << '\a';
            return output.str();
        }

        std::size_t display_width(const std::string &text) {
            std::size_t width = 0;
            for (std::size_t index = 0; index < text.size(); ++index) {
                const auto byte = static_cast<unsigned char>(text[index]);
                if (byte == 27 && index + 1 < text.size()) {
                    if (text[index + 1] == '[') {
                        index += 2;
                        while (index < text.size() && !(text[index] >= '@' && text[index] <= '~')) {
                            ++index;
                        }
                    } else if (text[index + 1] == ']') {
                        while (index < text.size() && text[index] != '\a') {
                            ++index;
                        }
                    }
                } else if ((byte & 0xC0) != 0x80) {
                    // Only ASCII and the one-column block glyph are used inside frames.
                    ++width;
                }
            }
            return width;
        }

        std::string item_name(int id) {
            return id < 0 ? "none" : items[id].name;
        }

        std::string order(const RunState &run) {
            return run.you.spd >= run.enemy.spd ? "YOU > ENEMY" : "ENEMY > YOU";
        }

        bool charging(Action action) {
            return action.stage == Action::Manual &&
                   (action.skill == 16 || action.skill == 17 || action.skill == 19);
        }

        std::string action_label(Action action) {
            if (action.stage == Action::Rest) {
                return "REST";
            }
            std::string label = skills[action.skill].name;
            for (char &character : label) {
                if (character >= 'a' && character <= 'z') {
                    character = static_cast<char>(character - 'a' + 'A');
                }
            }
            if (action.stage == Action::Release) {
                return label + " / RELEASE";
            }
            return label + (charging(action) ? " / CHARGE" : "");
        }
    }

    std::string Renderer::normal() const {
        if (options.color == Options::Plain) {
            return {};
        }
        return rgb(0xDCE4F2) + (options.color == Options::Osc ? "\x1b[48;5;16m" : rgb(0x0B1020, true));
    }

    std::string Renderer::ink(int index) const {
        if (options.color == Options::Plain) {
            return {};
        }
        if (options.color == Options::Osc) {
            return "\x1b[38;5;" + std::to_string(index) + "m\x1b[48;5;16m";
        }
        return rgb(ui_colors.at(static_cast<std::size_t>(index - 34))) + rgb(0x0B1020, true);
    }

    std::string Renderer::row(const std::string &content) const {
        const auto width = display_width(content);
        const std::string padding(width < 74 ? 74 - width : 0, ' ');
        return ink(34) + "|" + normal() + content + normal() + padding + ink(34) + "|" +
               (options.color == Options::Plain ? "" : reset) + "\n";
    }

    std::string Renderer::heading(const std::string &label) const {
        return border() + row(" " + label);
    }

    std::string Renderer::border() const {
        return ink(34) + "+" + std::string(74, '-') + "+" +
               (options.color == Options::Plain ? "" : reset) + "\n";
    }

    std::string Renderer::gauge(int current, int maximum, int color) const {
        const int filled = maximum > 0 ? std::clamp(14 * current / maximum, 0, 14) : 0;
        return ink(34) + "[" + ink(color) + std::string(filled, '#') + ink(34) +
               std::string(14 - filled, '-') + "]" + normal() + " " +
               std::to_string(current) + "/" + std::to_string(maximum);
    }

    std::vector<std::string> Renderer::sprite(int character) {
        const int source = character >= 11 ? 5 : character == 10 ? 0 : enemies[character].art;
        std::array<std::string, 16> pixels;
        for (int y = 0; y < 16; ++y) {
            pixels[y] = art[source][y];
        }
        auto put = [&](int x, int y, char pixel) { pixels.at(y).at(x) = pixel; };
        switch (character) {
        case 10: put(7, 8, 'l'); put(8, 8, 'l'); break;
        case 9:
            put(6, 1, 'a'); put(9, 1, 'a'); put(7, 0, 'a'); put(8, 0, 'a');
            break;
        case 5:
            put(3, 0, '.'); put(12, 0, '.'); put(2, 1, '.'); put(13, 1, '.');
            break;
        case 3:
            for (int y = 12; y < 16; ++y) {
                pixels[y].assign(16, '.');
            }
            put(7, 13, 'a'); put(8, 14, 'a');
            break;
        case 4:
            put(7, 0, '.'); put(6, 1, '.'); put(8, 1, '.'); put(7, 5, 's');
            break;
        case 7:
            put(5, 2, 'a'); put(9, 2, 'a'); put(3, 4, 'a'); put(11, 4, 'a');
            break;
        case 6:
            for (int y = 3; y <= 10; ++y) {
                put(14, y, 'l');
            }
            put(13, 6, 'a'); put(13, 7, 'a');
            break;
        case 8:
            put(2, 1, 'l'); put(2, 2, 'l'); put(2, 3, 'l'); put(3, 4, 'l');
            break;
        case 12:
            for (int y = 0; y <= 5; ++y) {
                pixels[y].assign(16, '.');
            }
            break;
        default: break;
        }

        const int palette_base = character >= 11 ? 28 : character == 10 ? 22 : 16;
        std::string definitions;
        if (options.color == Options::Osc) {
            for (int color = 0; color < 6; ++color) {
                used[palette_base + color] = true;
                definitions += define_palette(palette_base + color, palettes[character][color]);
            }
        }
        std::vector<std::string> result(8);
        const std::string color_symbols = ".sblea";
        const std::string priority = ".sblae";
        const std::string ascii = " :O#+*";
        for (int y = 0; y < 16; y += 2) {
            std::string &line = result[y / 2];
            if (y == 0) {
                line += definitions;
            }
            if (options.color == Options::Plain) {
                for (int x = 0; x < 16; x += 2) {
                    std::size_t selected = 0;
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            selected = std::max(selected, priority.find(pixels[y + dy][x + dx]));
                        }
                    }
                    line.append(2, ascii.at(selected));
                }
                continue;
            }
            for (int x = 0; x < 16; ++x) {
                const auto upper = color_symbols.find(pixels[y][x]);
                const auto lower = color_symbols.find(pixels[y + 1][x]);
                if (options.color == Options::Osc) {
                    line += "\x1b[38;5;" + std::to_string(palette_base + upper) +
                            "m\x1b[48;5;" + std::to_string(palette_base + lower) + "m";
                } else {
                    line += rgb(palettes[character][upper]) + rgb(palettes[character][lower], true);
                }
                line += "▀";
            }
            line += reset;
            line += normal();
        }
        return result;
    }

    std::string Renderer::status(const RunState &run) const {
        const auto &fighter = run.you;
        std::string output = heading("FIGHTER STATUS");
        output += row(" HP " + gauge(fighter.hp, fighter.max_hp, fighter.hp * 4 <= fighter.max_hp ? 40 : 35) +
                      "    MP " + gauge(fighter.mp, fighter.max_mp, 36));
        output += row(ink(37) + " SPD " + std::to_string(fighter.spd) + normal() +
                      (run.screen == Screen::Battle ? "   ORDER: " + order(run) : "") +
                      ink(38) + "   GOLD " + std::to_string(run.gold));
        output += row(" FOCUS " + std::string(fighter.focus ? "ON" : "-") + ink(40) +
                      "   BURN " + (fighter.burn ? std::to_string(fighter.burn) : "-") +
                      normal() + "   BANK " + gauge(fighter.bank, 12, 38));
        output += row(ink(39) + " ARMOR " + item_name(run.armor) + "   CHARM " + item_name(run.charm));
        output += border();
        output += normal() + "HP: 体力 / MP: 技の力 / SPD: 素早さ / GOLD: 所持金\n";
        output += "防具: " + std::string(run.armor < 0 ? "なし" : items[run.armor].japanese) +
                  " / 装飾: " + (run.charm < 0 ? "なし" : items[run.charm].japanese) + "\n";
        return output;
    }

    std::string Renderer::route_map(const RunState &run) const {
        const auto available = routes(run);
        auto node_label = [&](int node) {
            if (std::find(run.defeated.begin(), run.defeated.end(), node) != run.defeated.end()) {
                return ink(35) + "[DONE] " + enemies[run.nodes[node].enemy].name;
            }
            if (run.screen == Screen::Battle && run.node == node) {
                return ink(37) + "[HERE] " + enemies[run.nodes[node].enemy].name;
            }
            const auto choice = std::find(available.begin(), available.end(), node);
            if (run.screen == Screen::Route && choice != available.end()) {
                return ink(38) + "[" + std::to_string(std::distance(available.begin(), choice) + 1) +
                       "] " + enemies[run.nodes[node].enemy].name;
            }
            return ink(41) + "[LOCK] " + enemies[run.nodes[node].enemy].name;
        };
        auto pair = [&](int left, int right) {
            auto label = node_label(left);
            label += std::string(35 - std::min<std::size_t>(35, display_width(label)), ' ');
            return row(" " + label + node_label(right));
        };
        std::string output = heading("TOURNAMENT / ENTRY");
        output += row("             /                              \\");
        output += pair(0, 1);
        output += row("        /         \\                    /         \\");
        output += row(" " + node_label(2) + "  -> LEFT CHILD A");
        output += row(" " + node_label(3) + "  -> LEFT CHILD B");
        output += row(" " + node_label(4) + "  -> RIGHT CHILD A");
        output += row(" " + node_label(5) + "  -> RIGHT CHILD B");
        const bool booth_one = run.bout == 2 && (run.screen == Screen::Booth || run.screen == Screen::Shop);
        output += row(ink(39) + std::string(booth_one ? " [HERE] " : " ") +
                      "[LEARN] BOOTH I" + ink(38) + " -> [SHOP]");
        output += pair(6, 7);
        const bool booth_two = run.bout == 3 && (run.screen == Screen::Booth || run.screen == Screen::Shop);
        output += row(ink(39) + std::string(booth_two ? " [HERE] " : " ") +
                      "[LEARN] BOOTH II" + ink(38) + " -> [SHOP]");
        output += row(" " + node_label(8));
        output += row(" " + node_label(9));
        output += border();
        return output;
    }

    std::string Renderer::opponents(const RunState &run) const {
        std::string output = normal() + "次の対戦候補（技の効果も確認して選んでください）\n";
        const auto choices = routes(run);
        for (std::size_t index = 0; index < choices.size(); ++index) {
            const auto &node = run.nodes[choices[index]];
            const auto &enemy = enemies[node.enemy];
            output += ink(38) + std::to_string(index + 1) + ": " + enemy.name + normal() +
                      "  HP" + std::to_string(enemy.hp) + ink(37) + " SPD" + std::to_string(enemy.spd) + "\n";
            for (int id : node.slots) {
                output += normal() + "  " + skills[id].name + " / " + skills[id].japanese +
                          " / " + std::to_string(skills[id].mp) + "MP / " + skills[id].description + "\n";
            }
        }
        return output;
    }

    std::string Renderer::skill_description(const RunState &run, int id) const {
        std::string equipped;
        for (int slot = 0; slot < 3; ++slot) {
            if (run.you.slots[slot] == id) {
                equipped += " [" + std::to_string(slot + 1) + "]";
            }
        }
        if (equipped.empty()) {
            equipped = id < 3 ? " [基本技]" : " [未装備]";
        }
        return normal() + skills[id].japanese + " / " + skills[id].name + " / " +
               std::to_string(skills[id].mp) + "MP" + equipped + "\n  " + skills[id].description + "\n";
    }

    std::string Renderer::battle_screen(const RunState &run) {
        const auto &enemy = run.enemy;
        const auto &you = run.you;
        std::string output = heading(std::string("VOID PIT / ") + (run.practice ? "PRACTICE" : "RANDOM RUN") +
                                     "  BOUT " + std::to_string(run.bout + 1) + "/5  ROUND " +
                                     std::to_string(run.round) + "/20");
        const auto picture = sprite(run.nodes[run.node].enemy);
        std::array<std::string, 8> details;
        details[0] = enemies[run.nodes[run.node].enemy].name;
        details[1] = "HP " + gauge(enemy.hp, enemy.max_hp, enemy.hp * 4 <= enemy.max_hp ? 40 : 35);
        details[2] = ink(38) + "NEXT: " + action_label(run.forecast);
        if (charging(run.forecast)) {
            details[3] = "THEN: RELEASE " + std::to_string(run.forecast.skill == 16 ? 36 : 48) + " -> REST";
            details[4] = "No damage this round.";
        } else if (run.forecast.stage == Action::Release) {
            details[3] = "POWER " + std::to_string(enemy.pending.power) + " -> REST";
        }
        details[5] = ink(37) + "SPD " + std::to_string(enemy.spd) + normal() + "  MP " + std::to_string(enemy.mp);
        details[6] = "FOCUS " + std::string(enemy.focus ? "ON" : "-") + ink(40) +
                     "  BURN " + (enemy.burn ? std::to_string(enemy.burn) : "-");
        details[7] = "ORDER: " + order(run);
        for (int line = 0; line < 8; ++line) {
            output += row("   " + picture[line] + normal() + "     " + details[line]);
        }
        output += row(" YOU HP " + gauge(you.hp, you.max_hp, you.hp * 4 <= you.max_hp ? 40 : 35) +
                      "    MP " + gauge(you.mp, you.max_mp, 36));
        output += row(ink(37) + " SPD " + std::to_string(you.spd) + ink(38) + "  GOLD " +
                      std::to_string(run.gold) + normal() + "  FOCUS " + (you.focus ? "ON" : "-") +
                      "  BANK " + std::to_string(you.bank) + "/12" + ink(40) +
                      "  BURN " + (you.burn ? std::to_string(you.burn) : "-"));
        if (you.pending.power > 0) {
            output += row(" YOU: " + action_label({you.pending.skill, Action::Release}) +
                          " / POWER " + std::to_string(you.pending.power) + " -> REST / next");
        } else if (you.rest) {
            output += row(" YOU: REST / next");
        }
        std::string equipped;
        for (int slot = 0; slot < 3; ++slot) {
            const int id = you.slots[slot];
            equipped += " [" + std::to_string(slot + 1) + "] " + skills[id].name + " " +
                        std::to_string(skills[id].mp) + "MP ";
        }
        output += row(equipped);
        output += row(" attack / guard / wait / skills / book / status / help / quit");
        for (const auto &entry : run.log) {
            output += row(" LOG: " + entry.substr(0, 68));
        }
        output += border();
        output += normal() + "通常攻撃: attack / 防御: guard / 待機: wait\n";
        output += "NEXT: 次の敵行動 / ORDER: 行動順 / FOCUS: 集中 / BANK: 防御の蓄積\n";
        if (run.forecast.stage == Action::Rest) {
            output += "相手は休んでいます。この回は行動しません。\n";
        } else if (charging(run.forecast)) {
            output += "相手はこの回に力をため、次の回に発動します。\n";
        } else if (run.forecast.stage == Action::Release) {
            output += "相手の溜めた攻撃が発動します。発動後は1回休息します。\n";
        } else {
            output += std::string("敵の行動: ") + skills[run.forecast.skill].japanese + " / " +
                      skills[run.forecast.skill].description + "\n";
        }
        if (run.round == run.guard_hint_round) {
            output += "相手は休んでいる。通常攻撃で反撃できる。\n";
        }
        if (run.round == run.charge_hint_round) {
            output += "相手は力をためている。次の攻撃を防ぐ準備をしよう。\n";
        }
        if (you.pending.power > 0 || you.rest) {
            output += "自分の発動・休息を next で進めてください。情報表示は自由に使えます。\n";
        }
        return output;
    }

    std::string Renderer::shop_screen(const RunState &run) const {
        const auto preview = stats(run.preview_armor, run.preview_charm);
        std::string output = heading(run.bout == 2 ? "[HERE] SHOP I" : "[HERE] SHOP II");
        output += row(ink(38) + " GOLD " + std::to_string(run.gold) + ink(39) +
                      "   ARMOR " + item_name(run.preview_armor) + "   CHARM " + item_name(run.preview_charm));
        output += row(" PREVIEW HPmax " + std::to_string(preview[0]) + "  MPmax " +
                      std::to_string(preview[1]) + "  SPD " + std::to_string(preview[2]));
        output += border();
        output += normal() + "装備は店を出るときに確定します。試着を繰り返しても回復は増えません。\n";
        const auto choices = shop_choices(run);
        for (std::size_t index = 0; index < choices.size(); ++index) {
            const int id = choices[index];
            output += ink(38) + std::to_string(index + 1) + ": " + normal();
            if (id < 0) {
                output += id == -1 ? "防具を外す\n" : "装飾を外す\n";
                continue;
            }
            const auto &item = items[id];
            const auto changed = stats(item.charm ? run.preview_armor : id,
                                       item.charm ? id : run.preview_charm);
            output += std::string(item.japanese) + " / " + item.name +
                      (run.owned[id] ? " [購入済み・装備変更]" : "  " + std::to_string(item.price) + " G") + "\n";
            output += "   HP " + std::to_string(preview[0]) + " -> " + std::to_string(changed[0]) +
                      "  MP " + std::to_string(preview[1]) + " -> " + std::to_string(changed[1]) +
                      "  SPD " + std::to_string(preview[2]) + " -> " + std::to_string(changed[2]) + "\n";
            output += std::string("   ") + item.description + "\n";
        }
        output += ink(38) + "0: 店を出る（最終装備を確定）\n" + normal();
        output += opponents(run);
        return output;
    }

    std::string Renderer::result_screen(const RunState &run, bool final) {
        std::string output = heading(run.won ? "CHAMPION" : "DEFEAT");
        if (run.won) {
            const bool gold = final && run.reward == RewardTier::Gold;
            for (const auto &line : sprite(gold ? 11 : 12)) {
                output += row("                           " + line);
            }
            if (options.color == Options::Plain && final) {
                output += gold ? R"(        .       .
    .   |\  /|  |   .
     \  | \/ |  |  /
      \_|____|__|_/
       /  GOLD   \
      (\  CROWN  /)
       \________/
           ||
        ___||___
       |________|
)" : R"(        __________
       /  SILVER  \
       \__________/
       (\        /)
        \______/
           ||
         __||__
        |______|
)";
            }
            if (final) {
                output += row(ink(38) + (gold ? " GOLD CROWN / PRIZE 10000 G / TITLE: VOID CROWN" :
                                                              " SILVER CUP / PRIZE 2500 G"));
                output += row(gold ? " The crown remembers your name." : " The season is over.");
            }
        } else {
            for (const auto &line : sprite(10)) {
                output += row("                           " + line);
            }
            output += row(" PRIZE 0 G");
        }
        const int points = score(run);
        const char rank = points >= 7500 ? 'S' : points >= 6000 ? 'A' : points >= 4000 ? 'B' : 'C';
        output += row(" SCORE " + std::to_string(points) + (run.won ? " / RANK " + std::string(1, rank) : ""));
        output += row(" MAX DAMAGE " + std::to_string(run.maximum_damage) + " / TAKEN " +
                      std::to_string(run.taken) + " / ROUNDS " + std::to_string(run.rounds));
        output += row(std::string(run.practice ? " PRACTICE" : " RANDOM RUN") + " / SEED " + seed_text(run.seed));
        for (const auto &entry : run.log) {
            output += row(" LOG: " + entry.substr(0, 68));
        }
        output += border();
        output += normal() + "SCORE: 得点 / MAX DAMAGE: 最大実ダメージ / TAKEN: 被ダメージ / ROUNDS: 総ラウンド\n";
        if (std::find(run.log.begin(), run.log.end(), "You learned REPRISE: borrow a charging strike.") != run.log.end()) {
            output += "借り技を覚えました。この大会での登録です。\n";
        }
        output += run.won && !final ? "Enter: 表彰へ\n" : "Enter: 終了\n";
        return output;
    }

    std::string Renderer::main_screen(const RunState &run, bool final) {
        switch (run.screen) {
        case Screen::Battle: return battle_screen(run);
        case Screen::Shop: return shop_screen(run);
        case Screen::Result: return result_screen(run, final);
        case Screen::Route:
            return route_map(run) + status(run) + opponents(run) + normal() + "対戦相手を番号で選んでください。\n";
        case Screen::Booth: {
            std::string output = route_map(run) + heading(run.bout == 2 ? "[HERE][LEARN] BOOTH I" : "[HERE][LEARN] BOOTH II");
            if (run.learned_here) {
                output += normal() + "この習得所では習得済みです。equipで装備変更できます。\n";
            } else {
                const auto choices = learnable(run);
                for (std::size_t index = 0; index < choices.size(); ++index) {
                    output += ink(39) + std::to_string(index + 1) + ": " + skill_description(run, choices[index]);
                }
            }
            return output + normal() + "0: 習得所を出る / equip: 覚えた技へ装備変更 / book: 登録技一覧\n";
        }
        case Screen::Discovery:
            return heading("TECHNIQUE DISCOVERED") + normal() +
                   "借り技を覚えました。発動前の予約を借り、自分の力として溜め直せます。\n" +
                   skill_description(run, 19) + "交換する枠を1・2・3で選択 / 0: 交換せず進む\n";
        case Screen::Closed: return {};
        }
        return {};
    }

    std::string Renderer::information(const RunState &run, const Presentation &display) const {
        if (display.view == View::Help) {
            return heading("HELP") + normal() + help_text();
        }
        if (display.view == View::Status) {
            return status(run);
        }
        std::string output = heading(display.view == View::Recent ? "RECENT" : "TECHNIQUES");
        if (display.view == View::Recent) {
            if (run.history.empty()) {
                output += normal() + "まだ戦闘コマンドの記録はありません。\n";
            }
            for (auto entry = run.history.rbegin(); entry != run.history.rend(); ++entry) {
                output += normal() + std::string(skills[*entry].japanese) + " / " + skills[*entry].name + "\n";
            }
        } else if (display.view == View::Skills) {
            for (int id : run.you.slots) {
                output += skill_description(run, id);
            }
            output += normal() + "基本技: attack（通常攻撃） / guard（防御） / wait（待機）\n";
        } else if (display.view == View::Completion) {
            for (int id : completion_choices(run, display.prefix)) {
                output += skill_description(run, id);
            }
            output += normal() + "候補は自動実行しません。backで戻って入力してください。\n";
        } else {
            for (int id = 0; id < static_cast<int>(skills.size()); ++id) {
                if (run.registered[id]) {
                    output += skill_description(run, id);
                }
            }
        }
        return output;
    }

    std::string Renderer::render(const RunState &run, const Presentation &display) {
        std::string output;
        if (options.clear) {
            output = "\x1b[2J\x1b[H";
        } else if (drawn) {
            output.assign(8, '\n');
        }
        if (options.color == Options::Osc && !drawn) {
            output += define_palette(16, 0x0B1020);
            used[16] = true;
            for (int index = 34; index <= 41; ++index) {
                output += define_palette(index, ui_colors[index - 34]);
                used[index] = true;
            }
        }
        drawn = true;
        output += normal();
        output += title;
        switch (display.view) {
        case View::Main:
            if (run.won && run.enemy.hp == 0 && run.screen != Screen::Result && run.screen != Screen::Shop) {
                output += heading("BOUT CLEAR");
                for (const auto &entry : run.log) {
                    output += row(" LOG: " + entry.substr(0, 68));
                }
                output += border() + normal() + "勝利報酬を獲得し、HP24・MP12を上限まで回復しました。\n";
            }
            output += main_screen(run, display.result_final);
            break;
        case View::Quit:
            output += heading("QUIT") + normal() + "終了しますか (1: 終了 / 2: 戻る):\n";
            break;
        case View::Purchase: {
            const auto &item = items.at(display.selected);
            const auto preview = stats(item.charm ? run.preview_armor : display.selected,
                                       item.charm ? display.selected : run.preview_charm);
            output += heading("PURCHASE PREVIEW") + normal() + std::string(item.japanese) +
                      " / " + std::to_string(item.price) + " G / 所持金 " + std::to_string(run.gold) + " G\n";
            output += row(" HPmax " + std::to_string(preview[0]) + " / MPmax " + std::to_string(preview[1]) +
                          " / SPD " + std::to_string(preview[2]));
            output += normal() + std::string(item.description) + "\n購入しますか (1: 購入 / 2: 戻る):\n";
            break;
        }
        case View::Slot:
            output += heading("TECHNIQUE SLOT") + skill_description(run, display.selected);
            for (int slot = 0; slot < 3; ++slot) {
                output += normal() + std::to_string(slot + 1) + ": " + skills[run.you.slots[slot]].japanese +
                          " / " + skills[run.you.slots[slot]].name + "\n";
            }
            output += "交換する枠を1・2・3で選択 / back: 戻る\n";
            break;
        case View::EquipSkills: {
            output += heading("EQUIP TECHNIQUE");
            const auto choices = known_skills(run);
            for (std::size_t index = 0; index < choices.size(); ++index) {
                output += normal() + std::to_string(index + 1) + ": " + skill_description(run, choices[index]);
            }
            output += normal() + "装備したい技を番号で選択 / 0: 戻る\n";
            break;
        }
        default:
            output += information(run, display) + normal() + "back: 戻る\n";
            break;
        }
        if (!display.message.empty()) {
            output += ink(40) + display.message + "\n";
        }
        output += ink(34) + "+" + std::string(74, '-') + "+\n";
        output += normal() + "help: 操作説明 / quit: 終了\n> ";
        if (options.color != Options::Plain) {
            output += reset;
        }
        return output;
    }

    std::string Renderer::progress(int bout) const {
        if (options.color != Options::Osc || !options.progress) {
            return {};
        }
        return "\x1b]9;4;1;" + std::to_string(std::clamp(bout * 20, 0, 100)) + "\a";
    }

    std::string Renderer::finish() {
        if (options.color == Options::Plain) {
            return {};
        }
        std::string output;
        if (options.color == Options::Osc) {
            for (int index = 16; index <= 41; ++index) {
                if (used[index]) {
                    output += "\x1b]104;" + std::to_string(index) + "\a";
                    used[index] = false;
                }
            }
            if (options.progress) {
                output += "\x1b]9;4;0\a";
            }
        }
        return output + reset;
    }

    std::string help_text() {
        return "入力してEnterを押すと進みます。急いで入力する必要はありません。\n"
               "1・2・3: 装備した技 / attack: 通常攻撃 / guard: 防御 / wait: 待機\n"
               "防御はその回の直接攻撃を半分にしてMPを4回復します。待機は防御せずMPを6回復します。\n"
               "HPは体力、MPは技の力、SPDは素早さ、GOLDは買物に使う所持金です。\n"
               "NEXTは敵の次行動。ORDERのYOUが自分、ENEMYが相手。同じSPDなら自分が先です。\n"
               "skills: 今使える技 / book: 覚えた技 / status: 能力・装備 / recent: 最近使った技\n"
               "? fi のように入力してEnterを押すと候補を表示します。候補は自動実行しません。\n"
               "情報表示や間違った入力では相手は動きません。backで元の画面に戻ります。\n"
               "nextが表示されたときは、nextを入力して進めてください。\n"
               "経路のHEREは現在地、DONEは通った場所、黄色の番号は選べる道、LOCKは閉じた道です。\n"
               "途中の習得所で技を1つ覚え、交換する枠を選びます。equipで既習得技へ変更できます。\n"
               "店では防具と装飾品を購入できます。能力の変化を見て選び、0で店を出ると装備が確定します。\n"
               "quitで終了確認を開き、1で終了、2または空行で戻ります。\n"
               "入力が途切れた場合は中断記録を保存します。表示されたファイルを--resumeで指定すると再開できます。\n"
               "保存失敗と表示されたときは画面に出たseedと操作列を残してください。\n";
    }
}
