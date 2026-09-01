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
    static Logger& instance() noexcept {
        static Logger logger;
        return logger;
    }

    template <typename... Args>
    void log(Level level, std::string_view file, int line, Args&&... args) {
        std::ostringstream ss;
        (ss << ... << std::forward<Args>(args));

        std::ostream& out = (level == Level::Error) ? std::cerr : std::cout;
        out << "[" << level_to_string(level) << "] [" << file << ":" << line << "] " << ss.str() << "\n";
    }

private:
    Logger() = default;

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
