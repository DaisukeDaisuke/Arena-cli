#include "lcg.hpp"
#include "session.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
    struct Arguments {
        arena::Options terminal;
        std::string resume_file;
        std::uint64_t seed = 0;
        bool seed_given = false;
        bool tests = false;
        bool help = false;
        bool plain_given = false;
    };

    Arguments parse_arguments(int argc, char **argv) {
        Arguments arguments;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--plain") {
                arguments.terminal.color = arena::Options::Plain;
                arguments.plain_given = true;
            } else if (option == "--color=rgb") {
                arguments.terminal.color = arena::Options::Rgb;
            } else if (option == "--color=osc") {
                arguments.terminal.color = arena::Options::Osc;
            } else if (option == "--redraw=append") {
                arguments.terminal.clear = false;
            } else if (option == "--redraw=clear") {
                arguments.terminal.clear = true;
            } else if (option == "--progress") {
                arguments.terminal.progress = true;
            } else if (option == "--self-test") {
                arguments.tests = true;
            } else if (option == "--help") {
                arguments.help = true;
            } else if (option == "--seed") {
                if (++index >= argc || !arena::parse_seed(argv[index], arguments.seed)) {
                    throw std::runtime_error("--seedには16桁の16進数を指定してください。");
                }
                arguments.seed_given = true;
            } else if (option == "--resume") {
                if (++index >= argc) {
                    throw std::runtime_error("--resumeには中断記録のファイル名が必要です。");
                }
                arguments.resume_file = argv[index];
            } else {
                throw std::runtime_error("起動オプションが不正です。--helpで確認してください。");
            }
        }
        if (arguments.plain_given) {
            arguments.terminal.color = arena::Options::Plain;
        }
        if (arguments.plain_given && arguments.terminal.clear) {
            throw std::runtime_error("--plainと--redraw=clearは併用できません。");
        }
        if (arguments.seed_given && !arguments.resume_file.empty()) {
            throw std::runtime_error("--seedと--resumeは併用できません。");
        }
        return arguments;
    }

    class TerminalOutput {
    public:
        arena::Renderer renderer;

        explicit TerminalOutput(arena::Options options) : renderer(options) {}
        ~TerminalOutput() {
            std::cout << renderer.finish() << std::flush;
        }
    };

    int save_and_exit(const arena::Session &session) {
        std::string filename, error;
        if (arena::suspend_session(session, filename, error)) {
            std::cout << "\n入力が途切れたか操作上限に達したため、進行を保存しました。\n"
                      << "--resume " << filename << " で再開できます。\n"
                      << "SEED " << arena::seed_text(session.run.seed) << '\n';
            return 0;
        }
        std::cerr << "\n保存失敗: " << error << "\n以下の再開情報を残してください。\n";
        arena::write_record(std::cerr, session);
        return 1;
    }

    int play(const Arguments &arguments) {
        arena::Session session(0, false);
        if (!arguments.resume_file.empty()) {
            std::ifstream input(arguments.resume_file, std::ios::binary);
            std::string error;
            if (!arena::resume_session(input, session, error)) {
                throw std::runtime_error("再開できません: " + error);
            }
        } else {
            const auto seed = arguments.seed_given ? arguments.seed : arena::device_seed();
            session = arena::Session(seed, arguments.seed_given);
        }

        TerminalOutput terminal(arguments.terminal);
        std::string input;
        while (!session.closed) {
            std::cout << terminal.renderer.render(session.run, session.display) << std::flush;
            const auto result = arena::read_line(std::cin, input);
            if (result == arena::LineResult::End) {
                return save_and_exit(session);
            }
            if (result == arena::LineResult::TooLong) {
                session.display.message = "入力は64バイト以内にしてください。行全体を破棄しました。";
                continue;
            }
            const auto victories = session.run.defeated.size();
            session.accept(input);
            if (session.run.defeated.size() != victories) {
                std::cout << terminal.renderer.progress(static_cast<int>(session.run.defeated.size()));
            }
            if (session.limit_reached) {
                return save_and_exit(session);
            }
        }
        std::cout << "\n終了しました。\n";
        return 0;
    }
}

int main(int argc, char **argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        if (arguments.help) {
            std::cout << "VOID PIT: BORROWED CROWN / Windows Terminal\n"
                      << "--plain | --color=rgb | --color=osc\n"
                      << "--redraw=append | --redraw=clear\n"
                      << "--seed <16桁hex> | --resume <中断記録>\n"
                      << "--progress: OSCモードの試合後進捗表示\n\n" << arena::help_text();
            return 0;
        }
        std::string error;
        if (!arena::validate_data(error)) {
            throw std::runtime_error("ゲームデータの検証に失敗しました: " + error);
        }
        if (arguments.tests) {
            return arena::self_test();
        }
        return play(arguments);
    } catch (const std::exception &exception) {
        std::cerr << "起動・実行エラー: " << exception.what() << '\n';
        return 1;
    }
}
