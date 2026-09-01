#pragma once

#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>

namespace q3::log {

enum class Level {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    using ConsoleSink = void(*)(const char*);

    static Logger& instance() noexcept {
        static Logger logger;
        return logger;
    }

    void set_console_sink(ConsoleSink sink) noexcept {
        m_console_sink = sink;
    }

    template <typename... Args>
    void log(Level level, std::string_view file, int line, Args&&... args) {
        std::ostringstream ss;
        (ss << ... << std::forward<Args>(args));

        std::string formatted = std::string("[") + level_to_string(level) + "] [" + std::string(file) + ":" + std::to_string(line) + "] " + ss.str() + "\n";

        std::ostream& out = (level == Level::Error) ? std::cerr : std::cout;
        out << formatted;

        if (m_console_sink) {
            m_console_sink(formatted.c_str());
        }
    }

private:
    Logger() = default;
    ConsoleSink m_console_sink = nullptr;

    static constexpr const char* level_to_string(Level level) noexcept {
        switch (level) {
            case Level::Debug:   return "DEBUG";
            case Level::Info:    return "INFO";
            case Level::Warning: return "WARN";
            case Level::Error:   return "ERROR";
        }
        return "LOG";
    }
};

} // namespace q3::log

#ifndef NDEBUG
    #define LOG_DEBUG(...) q3::log::Logger::instance().log(q3::log::Level::Debug, __FILE__, __LINE__, __VA_ARGS__)
    #define LOG_INFO(...)  q3::log::Logger::instance().log(q3::log::Level::Info,  __FILE__, __LINE__, __VA_ARGS__)
    #define LOG_WARN(...)  q3::log::Logger::instance().log(q3::log::Level::Warning, __FILE__, __LINE__, __VA_ARGS__)
    #define LOG_ERROR(...) q3::log::Logger::instance().log(q3::log::Level::Error, __FILE__, __LINE__, __VA_ARGS__)
#else
    #define LOG_DEBUG(...) ((void)0)
    #define LOG_INFO(...)  ((void)0)
    #define LOG_WARN(...)  ((void)0)
    #define LOG_ERROR(...) ((void)0)
#endif
