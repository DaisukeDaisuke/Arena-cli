#pragma once
#include "game.h"
#include <istream>
#include <ostream>

namespace arena {
    constexpr std::size_t operation_limit = 4096;
    constexpr std::size_t input_limit = 64;
    constexpr const char *save_version = "VOID-PIT-1";

    enum class LineResult { Line, TooLong, End };

    LineResult read_line(std::istream &input, std::string &line);
    bool parse_seed(const std::string &text, std::uint64_t &seed);
    std::vector<int> known_skills(const RunState &run);
    std::vector<int> shop_choices(const RunState &run);
    std::vector<int> completion_choices(const RunState &run, const std::string &prefix);

    class Session {
        View return_view = View::Main;
        View quit_return_view = View::Main;

        bool commit(const std::string &command);
        void select_main(const std::string &input);
        void select_slot(const std::string &input);
        void select_purchase(const std::string &input);
        void select_equipment(const std::string &input);

    public:
        RunState run;
        Presentation display;
        std::vector<std::string> operations;
        bool closed = false;
        bool limit_reached = false;

        Session(std::uint64_t seed, bool practice);
        void accept(const std::string &input);
        bool replay(const std::string &command, std::string &error);
    };

    bool resume_session(std::istream &input, Session &session, std::string &error);
    void write_record(std::ostream &output, const Session &session);
    bool suspend_session(const Session &session, std::string &filename, std::string &error);
}
