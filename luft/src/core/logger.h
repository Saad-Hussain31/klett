#pragma once

#include <cstdio>
#include <mutex>
#include <string>
#include <atomic>
#include <chrono>

namespace luft
{

    enum class LogLevel : int
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error,
        Fatal
    };

    inline const char *log_level_name(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warn:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        }
        return "?????";
    }

    class Logger
    {
    public:
        static Logger &instance();

        // Non-copyable, non-movable
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;

        void set_level(LogLevel level);
        void set_file(const std::string &path);
        void set_console_enabled(bool enabled);
        void set_rate_limit(int max_per_second);

        void log(LogLevel level, const char *file, int line, const char *fmt, ...)
            __attribute__((format(printf, 5, 6)));

        void flush();

        [[nodiscard]] LogLevel level() const { return min_level_.load(std::memory_order_relaxed); }

    private:
        Logger();
        ~Logger();

        std::atomic<LogLevel> min_level_{LogLevel::Info};
        std::atomic<bool> console_enabled_{true};
        std::atomic<int> rate_limit_{0}; // 0 = unlimited

        FILE *file_{nullptr};
        std::mutex mutex_;

        // Rate limiting state
        std::atomic<int> msg_count_{0};
        std::chrono::steady_clock::time_point rate_window_start_;

        // Flush control
        std::chrono::steady_clock::time_point last_flush_;

        void write_line(const char *buf, int len, LogLevel level);
        bool check_rate_limit();
    };

} // namespace luft

// ──────────────────────────────────────────────
// Convenience macros — early-out before any formatting
// ──────────────────────────────────────────────

#define LOG_TRACE(...)                                                                                \
    do                                                                                                \
    {                                                                                                 \
        if (::luft::Logger::instance().level() <= ::luft::LogLevel::Trace)                            \
            ::luft::Logger::instance().log(::luft::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define LOG_DEBUG(...)                                                                                \
    do                                                                                                \
    {                                                                                                 \
        if (::luft::Logger::instance().level() <= ::luft::LogLevel::Debug)                            \
            ::luft::Logger::instance().log(::luft::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define LOG_INFO(...)                                                                                \
    do                                                                                               \
    {                                                                                                \
        if (::luft::Logger::instance().level() <= ::luft::LogLevel::Info)                            \
            ::luft::Logger::instance().log(::luft::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define LOG_WARN(...)                                                                                \
    do                                                                                               \
    {                                                                                                \
        if (::luft::Logger::instance().level() <= ::luft::LogLevel::Warn)                            \
            ::luft::Logger::instance().log(::luft::LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define LOG_ERROR(...)                                                                                \
    do                                                                                                \
    {                                                                                                 \
        if (::luft::Logger::instance().level() <= ::luft::LogLevel::Error)                            \
            ::luft::Logger::instance().log(::luft::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)
