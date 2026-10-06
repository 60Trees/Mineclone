// <AI>

#pragma once

#include <ctime>
#include <functional>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

#define BLACK "\033[30m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define WHITE "\033[37m"

#define GREY "\033[90m"
#define BRIGHT_RED "\033[91m"
#define BRIGHT_GREEN "\033[92m"
#define BRIGHT_YELLOW "\033[93m"
#define BRIGHT_BLUE "\033[94m"
#define BRIGHT_MAGENTA "\033[95m"
#define BRIGHT_CYAN "\033[96m"
#define BRIGHT_WHITE "\033[97m"

#define RESET "\033[0m"

#define BOLD "\033[1m"
#define DIM "\033[2m"
#define ITALIC "\033[3m"
#define UNDERLINE "\033[4m"
#define BLINK "\033[5m"
#define REVERSE "\033[7m"
#define HIDDEN "\033[8m"
#define STRIKETHROUGH "\033[9m"

#define CLEAR "\033[2J\033[H"

#define TIMESTAMP ::logger::timestamp()

namespace logger {

    using string_function = std::function<std::string()>;
    using string_source = std::variant<std::string, string_function>;

    // ------------------------------------------------------------
    // Get the current local time as:
    //
    //     HH:MM:SS
    //
    // using a 24-hour clock.
    // ------------------------------------------------------------

    inline std::string timestamp() {
        std::time_t now = std::time(nullptr);

        std::tm local_time{};

#if defined(_WIN32)
        localtime_s(&local_time, &now);
#else
        localtime_r(&now, &local_time);
#endif

        char buffer[9];

        std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local_time);

        return buffer;
    }

    // ------------------------------------------------------------
    // Resolve either:
    //
    //     std::string
    //
    // or:
    //
    //     std::function<std::string()>
    // ------------------------------------------------------------

    inline std::string resolve(const string_source& source) {
        if (const auto* string = std::get_if<std::string>(&source))
            return *string;

        if (const auto* function = std::get_if<string_function>(&source))
            return (*function)();

        return {};
    }

    class Logger {
        public:
        string_source prefix = std::string{};
        string_source postfix = std::string{};

        bool enabled = true;
        bool doesflush = false;

        string_source newline_edit_middle = std::string{};
        string_source newline_edit_end = std::string{};

        std::ostream& output;

        Logger(string_source prefix = std::string{},
            string_source postfix = std::string{}, bool enabled = true,
            bool doesflush = false,
            string_source newline_edit_middle = std::string{},
            string_source newline_edit_end = std::string{},
            std::ostream& output = std::cout)
            : prefix(std::move(prefix)),
              postfix(std::move(postfix)),
              enabled(enabled),
              doesflush(doesflush),
              newline_edit_middle(std::move(newline_edit_middle)),
              newline_edit_end(std::move(newline_edit_end)),
              output(output) {}

        Logger(std::ostream& output)
            : prefix(""),
              postfix("\n"),
              enabled(true),
              doesflush(false),
              newline_edit_middle(""),
              newline_edit_end(""),
              output(output) {}
        Logger(const Logger&) = default;
        Logger& operator=(const Logger&) = delete;

        const string_source& get_prefix() const { return prefix; }

        const string_source& get_postfix() const { return postfix; }

        const string_source& get_newline_edit_middle() const {
            return newline_edit_middle;
        }

        const string_source& get_newline_edit_end() const {
            return newline_edit_end;
        }

        class LogLine {
            private:
            Logger* logger;
            std::ostringstream buffer;

            bool active = true;

            void finish() {
                if (!active || logger == nullptr) return;

                active = false;

                if (!logger->enabled) return;

                std::string text = buffer.str();

                // ------------------------------------------------
                // Prefix
                // ------------------------------------------------

                logger->output << resolve(logger->prefix);

                // ------------------------------------------------
                // Newline processing
                // ------------------------------------------------

                if (!text.empty()) {
                    std::size_t last_newline = text.rfind('\n');

                    if (last_newline == std::string::npos) {
                        logger->output << text;

                    } else {
                        std::string middle =
                            resolve(logger->newline_edit_middle);

                        std::string end = resolve(logger->newline_edit_end);

                        std::size_t position = 0;

                        while (position < last_newline) {
                            std::size_t newline = text.find('\n', position);

                            if (newline == std::string::npos ||
                                newline >= last_newline)
                                break;

                            logger->output.write(text.data() + position,
                                static_cast<std::streamsize>(
                                    newline - position + 1));

                            logger->output << middle;

                            position = newline + 1;
                        }

                        if (position <= last_newline) {
                            logger->output.write(text.data() + position,
                                static_cast<std::streamsize>(
                                    last_newline - position + 1));
                        }

                        logger->output << end;

                        if (last_newline + 1 < text.size()) {
                            logger->output.write(text.data() + last_newline + 1,
                                static_cast<std::streamsize>(
                                    text.size() - last_newline - 1));
                        }
                    }
                }

                // ------------------------------------------------
                // Postfix
                // ------------------------------------------------

                logger->output << resolve(logger->postfix);

                // ------------------------------------------------
                // Flush
                // ------------------------------------------------

                if (logger->doesflush) logger->output.flush();
            }

            public:
            explicit LogLine(Logger* logger) : logger(logger) {}

            ~LogLine() { finish(); }

            // ----------------------------------------------------
            // Move support
            // ----------------------------------------------------

            LogLine(LogLine&& other) noexcept
                : logger(other.logger),
                  buffer(std::move(other.buffer)),
                  active(other.active) {
                other.logger = nullptr;
                other.active = false;
            }

            LogLine& operator=(LogLine&& other) noexcept {
                if (this == &other) return *this;

                finish();

                logger = other.logger;
                buffer = std::move(other.buffer);
                active = other.active;

                other.logger = nullptr;
                other.active = false;

                return *this;
            }

            LogLine(const LogLine&) = delete;
            LogLine& operator=(const LogLine&) = delete;

            // ----------------------------------------------------
            // Regular values
            // ----------------------------------------------------

            template <typename T>
            LogLine& operator<<(const T& value) {
                if (logger != nullptr && logger->enabled) buffer << value;

                return *this;
            }

            // ----------------------------------------------------
            // ostream manipulators
            // ----------------------------------------------------

            LogLine& operator<<(std::ostream& (*manipulator)(std::ostream&)) {
                if (logger != nullptr && logger->enabled) manipulator(buffer);

                return *this;
            }

            LogLine& operator<<(std::ios& (*manipulator)(std::ios&)) {
                if (logger != nullptr && logger->enabled) manipulator(buffer);

                return *this;
            }

            LogLine& operator<<(std::ios_base& (*manipulator)(std::ios_base&)) {
                if (logger != nullptr && logger->enabled) manipulator(buffer);

                return *this;
            }
        };

        // --------------------------------------------------------
        // Begin a log expression
        // --------------------------------------------------------

        template <typename T>
        LogLine operator<<(const T& value) {
            LogLine line(this);
            line << value;
            return line;
        }

        LogLine operator<<(std::ostream& (*manipulator)(std::ostream&)) {
            LogLine line(this);
            line << manipulator;
            return line;
        }

        LogLine operator<<(std::ios& (*manipulator)(std::ios&)) {
            LogLine line(this);
            line << manipulator;
            return line;
        }

        LogLine operator<<(std::ios_base& (*manipulator)(std::ios_base&)) {
            LogLine line(this);
            line << manipulator;
            return line;
        }
    };

}  // namespace logger

// </AI>
