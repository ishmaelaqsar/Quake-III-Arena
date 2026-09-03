#pragma once

// Engine logger (checklist 02 phase B4).
//
// Three properties matter here, and the first version of this file had none of them:
//
//   1. Warnings and errors survive a release build. The macros used to expand to `((void)0)`
//      whenever `NDEBUG` was defined, which is every RelWithDebInfo and Release build, so a
//      shipping binary reported nothing at all. Only the two verbose levels are strippable now,
//      and only through an explicit `Q3_LOG_STRIP_VERBOSE`.
//   2. A line costs nothing when its level is filtered out. The level is checked before any
//      formatting, so a `LOG_DEBUG` in a hot path is an atomic load and a branch.
//   3. The console sink is only ever called on the main thread. `CL_ConsolePrint` walks the
//      console ring buffer with no lock, so a line logged from a worker is queued and delivered
//      by `flush_queued()` from `Sys_SubsystemFrame`. Checklist 05 step T1 replaces this queue
//      with the general MainThreadQueue; keep this interface so that swap stays a small change.
//
// Lines carry the basename of `__FILE__`, not the full path, so build directories do not leak
// into a user's console.

#include <atomic>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "threading/thread_affinity.hpp"
#include "threading/main_thread_queue.hpp"

namespace q3::log {

enum class Level {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
};

// Applied to __FILE__ in the macros. Constant-folds, so no path work happens at run time.
constexpr std::string_view basename(std::string_view path) noexcept {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

class Logger {
public:
    using ConsoleSink = void (*)(const char *);

    // Lines logged off the main thread beyond this many are dropped rather than grown without
    // bound. The drop is reported once on the next flush, so silence is never mistaken for calm.
    static constexpr std::size_t kMaxQueued = 1024;

    static Logger &instance() noexcept {
        static Logger logger;
        return logger;
    }

    void set_console_sink(ConsoleSink sink) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        console_sink_ = sink;
    }

    void set_level(Level level) noexcept { min_level_.store(level, std::memory_order_relaxed); }
    Level level() const noexcept { return min_level_.load(std::memory_order_relaxed); }

    bool enabled(Level level) const noexcept {
        return static_cast<int>(level) >= static_cast<int>(this->level());
    }

    // Call from Sys_SubsystemInit. Lines logged from any other thread are queued.
    void set_main_thread(std::thread::id id) noexcept {
        main_thread_ = id;
        has_main_thread_.store(true, std::memory_order_release);
    }

    template <typename... Args>
    void log(Level level, std::string_view file, int line, Args &&...args) {
        if (!enabled(level)) {
            return;  // before any formatting, which is the whole point of the check
        }

        // One scratch buffer per thread, reused, so a line does not construct a stream.
        thread_local std::ostringstream scratch;
        scratch.str(std::string());
        scratch.clear();
        scratch << '[' << level_to_string(level) << "] [" << file << ':' << line << "] ";
        (scratch << ... << std::forward<Args>(args));
        scratch << '\n';

        deliver(level, scratch.str());
    }

    // Deliver the lines that worker threads queued. Main thread only.
    void flush_queued() {
        q3::threading::MainThreadQueue::instance().drain_all();
    }

    // Test seam: the number of lines waiting for the next flush.
    std::size_t queued_count() const noexcept {
        return q3::threading::MainThreadQueue::instance().lossy_count();
    }

private:
    Logger() = default;

    void deliver(Level level, std::string formatted) {
        {
            std::lock_guard<std::mutex> lock(out_mutex_);
            std::ostream &out = (level == Level::Error) ? std::cerr : std::cout;
            out << formatted;
        }

        if (console_sink_ == nullptr) {
            return;
        }
        if (q3::threading::is_main_thread()) {
            call_sink(formatted);
            return;
        }

        static constexpr std::size_t kPoolSize = 1024;
        static constexpr std::size_t kMaxLineLength = 512;
        static char s_log_pool[kPoolSize][kMaxLineLength];
        static std::atomic<std::size_t> s_log_pool_head{0};

        std::size_t slot = s_log_pool_head.fetch_add(1, std::memory_order_relaxed) % kPoolSize;
        std::strncpy(s_log_pool[slot], formatted.c_str(), kMaxLineLength - 1);
        s_log_pool[slot][kMaxLineLength - 1] = '\0';

        q3::threading::FixedTask task;
        task.fn = [](void *payload) {
            const char *msg = *reinterpret_cast<const char **>(payload);
            Logger::instance().call_sink(msg);
        };
        const char *ptr = s_log_pool[slot];
        std::memcpy(task.payload, &ptr, sizeof(ptr));
        q3::threading::MainThreadQueue::instance().post_lossy(task);
    }

    void call_sink(const std::string &line) {
        ConsoleSink sink = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sink = console_sink_;
        }
        if (sink != nullptr) {
            sink(line.c_str());
        }
    }

    static constexpr const char *level_to_string(Level level) noexcept {
        switch (level) {
            case Level::Debug:   return "DEBUG";
            case Level::Info:    return "INFO";
            case Level::Warning: return "WARN";
            case Level::Error:   return "ERROR";
        }
        return "LOG";
    }

    std::mutex out_mutex_;
    std::mutex mutex_;
    ConsoleSink console_sink_ = nullptr;
    std::atomic<Level> min_level_{Level::Info};
    std::atomic<bool> has_main_thread_{false};
    std::thread::id main_thread_{};
};

}  // namespace q3::log

#define Q3_LOG_AT(level_, ...) \
    q3::log::Logger::instance().log((level_), q3::log::basename(__FILE__), __LINE__, __VA_ARGS__)

// Warnings and errors are always compiled: a release build that reports nothing is worse than
// a slightly larger one. Define Q3_LOG_STRIP_VERBOSE to remove the two verbose levels from a
// build entirely; otherwise they are present and filtered at run time by com_logLevel.
#ifdef Q3_LOG_STRIP_VERBOSE
    #define LOG_DEBUG(...) ((void)0)
    #define LOG_INFO(...)  ((void)0)
#else
    #define LOG_DEBUG(...) Q3_LOG_AT(q3::log::Level::Debug, __VA_ARGS__)
    #define LOG_INFO(...)  Q3_LOG_AT(q3::log::Level::Info, __VA_ARGS__)
#endif

#define LOG_WARN(...)  Q3_LOG_AT(q3::log::Level::Warning, __VA_ARGS__)
#define LOG_ERROR(...) Q3_LOG_AT(q3::log::Level::Error, __VA_ARGS__)
