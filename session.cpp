#include "session.h"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace arena {
    namespace {
        int menu_number(const std::string &text) {
            int number = -1;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
                return -1;
            }
            return number;
        }

        bool is_information(View view) {
            return view == View::Help || view == View::Skills || view == View::Book ||
                   view == View::Status || view == View::Recent || view == View::Completion;
        }
    }

    LineResult read_line(std::istream &input, std::string &line) {
        // Read the entire line while bounding allocation, including hostile redirected input.
        line.clear();
        bool too_long = false;
        bool received = false;
        char character = 0;
        while (input.get(character)) {
            received = true;
            if (character == '\n') {
                break;
            }
            if (line.size() <= input_limit) {
                line.push_back(character);
            } else {
                too_long = true;
            }
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (too_long || line.size() > input_limit) {
            line.clear();
            return LineResult::TooLong;
        }
        return received ? LineResult::Line : LineResult::End;
    }

    std::string normalize(std::string text) {
        const auto first = text.find_first_not_of(" \t\r\n\v\f");
        if (first == std::string::npos) {
            return {};
        }
        text = text.substr(first, text.find_last_not_of(" \t\r\n\v\f") - first + 1);
        for (char &character : text) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        return text;
    }

    std::string seed_text(std::uint64_t seed) {
        std::ostringstream output;
        output << std::hex << std::setfill('0') << std::setw(16) << seed;
        return output.str();
    }

    bool parse_seed(const std::string &text, std::uint64_t &seed) {
        if (text.size() != 16) {
            return false;
        }
        const auto result = std::from_chars(text.data(), text.data() + text.size(), seed, 16);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    std::vector<int> known_skills(const RunState &run) {
        std::vector<int> result;
        for (int id = 3; id < static_cast<int>(skills.size()); ++id) {
            if (run.registered[id]) {
                result.push_back(id);
            }
        }
        return result;
    }

    std::vector<int> shop_choices(const RunState &run) {
        std::vector<int> result;
        for (int id = 0; id < static_cast<int>(items.size()); ++id) {
            if (!items[id].second || run.bout == 3 || run.owned[id]) {
                result.push_back(id);
            }
        }
        result.push_back(-1); // Empty armor slot.
        result.push_back(-2); // Empty charm slot.
        return result;
    }

    std::vector<int> completion_choices(const RunState &run, const std::string &prefix) {
        std::vector<int> result{0, 1, 2};
        for (int id : run.you.slots) {
            if (std::find(result.begin(), result.end(), id) == result.end()) {
                result.push_back(id);
            }
        }
        result.erase(std::remove_if(result.begin(), result.end(), [&](int id) {
            return std::string(skills[id].name).compare(0, prefix.size(), prefix) != 0;
        }), result.end());
        auto recency = [&](int id) {
            auto found = std::find(run.history.rbegin(), run.history.rend(), id);
            return std::distance(run.history.rbegin(), found);
        };
        std::sort(result.begin(), result.end(), [&](int left, int right) {
            if (recency(left) != recency(right)) {
                return recency(left) < recency(right);
            }
            return left < right;
        });
        return result;
    }

    Session::Session(std::uint64_t seed, bool practice) : run(new_run(seed, practice)) {}

    bool Session::commit(const std::string &command) {
        if (operations.size() >= operation_limit) {
            limit_reached = true;
            display.message = "操作記録の上限です。追加操作を受理せず中断記録を保存します。";
            return false;
        }
        std::string error;
        if (!operation(run, command, error)) {
            display.message = error;
            return false;
        }
        operations.push_back(command);
        display.view = View::Main;
        display.selected = -1;
        return true;
    }

    bool Session::replay(const std::string &command, std::string &error) {
        if (command.empty() || command.size() > input_limit || operations.size() >= operation_limit) {
            error = "中断記録の操作数・行長が不正です。";
            return false;
        }
        if (!operation(run, command, error)) {
            return false;
        }
        operations.push_back(command);
        return true;
    }

    void Session::select_slot(const std::string &input) {
        if (input == "back") {
            display.view = display.learning ? View::Main : View::EquipSkills;
            return;
        }
        const int slot = menu_number(input);
        if (slot < 1 || slot > 3) {
            display.message = "交換先を1・2・3で選んでください。backで戻れます。";
            return;
        }
        const std::string verb = display.learning ? "learn " : "equip-skill ";
        commit(verb + std::to_string(display.selected) + " " + std::to_string(slot));
    }

    void Session::select_purchase(const std::string &input) {
        if (input == "2" || input == "back") {
            display.view = View::Main;
        } else if (input == "1") {
            commit("buy " + std::to_string(display.selected));
        } else {
            display.message = "1: 購入 / 2: 戻る を選んでください。";
        }
    }

    void Session::select_equipment(const std::string &input) {
        if (input == "back" || input == "0") {
            display.view = View::Main;
            return;
        }
        const auto choices = known_skills(run);
        const int number = menu_number(input);
        if (number < 1 || number > static_cast<int>(choices.size())) {
            display.message = "一覧の番号を選んでください。";
            return;
        }
        display.selected = choices[number - 1];
        display.learning = false;
        display.view = View::Slot;
    }

    void Session::select_main(const std::string &input) {
        const int number = menu_number(input);
        switch (run.screen) {
        case Screen::Battle: {
            if (input == "next") {
                commit("next");
                return;
            }
            const int id = number >= 1 && number <= 3 ? run.you.slots[number - 1] : skill_id(input);
            if (id < 0) {
                display.message = "技名は完全一致です。skillsで使える技を確認できます。";
                return;
            }
            commit(std::string("use ") + skills[id].name);
            return;
        }
        case Screen::Route: {
            const auto choices = routes(run);
            if (number >= 1 && number <= static_cast<int>(choices.size())) {
                commit("route " + std::to_string(choices[number - 1]));
                return;
            }
            break;
        }
        case Screen::Booth: {
            if (input == "equip") {
                display.view = View::EquipSkills;
                return;
            }
            if (number == 0) {
                commit("leave-booth");
                return;
            }
            const auto choices = learnable(run);
            if (!run.learned_here && number >= 1 && number <= static_cast<int>(choices.size())) {
                display.selected = choices[number - 1];
                display.learning = true;
                display.view = View::Slot;
                return;
            }
            break;
        }
        case Screen::Shop: {
            if (number == 0) {
                commit("leave-shop");
                return;
            }
            const auto choices = shop_choices(run);
            if (number >= 1 && number <= static_cast<int>(choices.size())) {
                const int item = choices[number - 1];
                if (item < 0 || run.owned[item]) {
                    commit("equip-item " + std::to_string(item));
                } else {
                    display.selected = item;
                    display.view = View::Purchase;
                }
                return;
            }
            break;
        }
        case Screen::Discovery:
            if (number == 0) {
                commit("leave-booth");
                return;
            }
            if (number >= 1 && number <= 3) {
                commit("equip-skill 19 " + std::to_string(number));
                return;
            }
            break;
        case Screen::Result:
            if (input.empty() || input == "next") {
                if (run.won && !display.result_final) {
                    display.result_final = true;
                } else {
                    closed = true;
                }
                return;
            }
            display.message = "Enterで進んでください。";
            return;
        case Screen::Closed:
            closed = true;
            return;
        }
        display.message = "表示された選択肢の番号を入力してください。";
    }

    void Session::accept(const std::string &raw_input) {
        display.message.clear();
        if (raw_input.size() > input_limit) {
            display.message = "入力は64バイト以内にしてください。行全体を破棄しました。";
            return;
        }
        const std::string input = normalize(raw_input);
        if (display.view == View::Quit) {
            if (input == "1") {
                closed = true;
            } else if (input == "2" || input.empty() || input == "back") {
                display.view = quit_return_view;
            } else {
                display.message = "1: 終了 / 2: 戻る を選んでください。";
            }
            return;
        }
        if (input == "quit") {
            quit_return_view = display.view;
            display.view = View::Quit;
            return;
        }
        if (input.empty() && !(run.screen == Screen::Result && display.view == View::Main)) {
            return;
        }

        View requested = View::Main;
        if (input == "help") requested = View::Help;
        if (input == "skills") requested = View::Skills;
        if (input == "book") requested = View::Book;
        if (input == "status") requested = View::Status;
        if (input == "recent") requested = View::Recent;
        if (input == "?" || input.compare(0, 2, "? ") == 0) {
            requested = View::Completion;
            display.prefix = input.size() > 2 ? normalize(input.substr(2)) : "";
        }
        if (requested != View::Main) {
            if (!is_information(display.view)) {
                return_view = display.view;
            }
            display.view = requested;
            return;
        }
        if (is_information(display.view)) {
            if (input == "back") {
                display.view = return_view;
            } else {
                display.message = "閲覧中です。backで元の画面に戻ってください。";
            }
            return;
        }
        switch (display.view) {
        case View::Slot: select_slot(input); return;
        case View::Purchase: select_purchase(input); return;
        case View::EquipSkills: select_equipment(input); return;
        default: select_main(input); return;
        }
    }

    void write_record(std::ostream &output, const Session &session) {
        output << save_version << '\n' << seed_text(session.run.seed) << '\n'
               << (session.run.practice ? "PRACTICE" : "RANDOM") << '\n';
        for (const auto &command : session.operations) {
            output << command << '\n';
        }
        output << "END " << session.operations.size() << '\n';
    }

    bool resume_session(std::istream &input, Session &session, std::string &error) {
        std::string version, seed, mode, line;
        if (read_line(input, version) != LineResult::Line || version != save_version ||
            read_line(input, seed) != LineResult::Line || read_line(input, mode) != LineResult::Line) {
            error = "中断記録の版またはヘッダーが不正です。";
            return false;
        }
        std::uint64_t parsed_seed = 0;
        if (!parse_seed(seed, parsed_seed) || (mode != "PRACTICE" && mode != "RANDOM")) {
            error = "中断記録のseedまたは大会種別が不正です。";
            return false;
        }
        Session restored(parsed_seed, mode == "PRACTICE");
        while (read_line(input, line) == LineResult::Line) {
            if (line == "END " + std::to_string(restored.operations.size())) {
                if (read_line(input, line) != LineResult::End) {
                    error = "中断記録の末尾に余分なデータがあります。";
                    return false;
                }
                session = std::move(restored);
                return true;
            }
            if (!restored.replay(line, error)) {
                error = "操作 " + std::to_string(restored.operations.size() + 1) + ": " + error;
                return false;
            }
        }
        error = "中断記録が途中で切れているか、行長が不正です。";
        return false;
    }

    bool suspend_session(const Session &session, std::string &filename, std::string &error) {
        const std::string stem = "arena-suspend-" + seed_text(session.run.seed) + "-" +
                                 std::to_string(session.operations.size());
        try {
            for (unsigned suffix = 0; suffix < 100000; ++suffix) {
                filename = stem + (suffix == 0 ? "" : "-" + std::to_string(suffix)) + ".txt";
                if (std::filesystem::exists(filename)) {
                    continue;
                }
                std::ofstream output(filename, std::ios::binary);
                write_record(output, session);
                output.close();
                if (!output) {
                    error = "中断記録の書込みに失敗しました。";
                    return false;
                }
                return true;
            }
            error = "中断記録の空きファイル名がありません。";
        } catch (const std::exception &exception) {
            error = exception.what();
        }
        return false;
    }
}
