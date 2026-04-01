#include "logger.h"

#include <cstdarg>
#include <ctime>
#include <thread>
#include <chrono>

namespace luft
{

    Logger &Logger::instance()
    {
        static Logger s_instance;
        return s_instance;
    }

    Logger::Logger()
        : rate_window_start_(std::chrono::steady_clock::now()), last_flush_(std::chrono::steady_clock::now())
    {
    }

    Logger::~Logger()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_)
        {
            std::fflush(file_);
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    void Logger::set_level(LogLevel level)
    {
        min_level_.store(level, std::memory_order_relaxed);
    }

    void Logger::set_file(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_)
        {
            std::fflush(file_);
            std::fclose(file_);
        }
        file_ = std::fopen(path.c_str(), "a");
    }

    void Logger::set_console_enabled(bool enabled)
    {
        console_enabled_.store(enabled, std::memory_order_relaxed);
    }

    void Logger::set_rate_limit(int max_per_second)
    {
        rate_limit_.store(max_per_second, std::memory_order_relaxed);
    }

    bool Logger::check_rate_limit()
    {
        int limit = rate_limit_.load(std::memory_order_relaxed);
        if (limit <= 0)
            return true;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - rate_window_start_);
        if (elapsed.count() >= 1)
        {
            msg_count_.store(0, std::memory_order_relaxed);
            rate_window_start_ = now;
        }
        return msg_count_.fetch_add(1, std::memory_order_relaxed) < limit;
    }

    void Logger::log(LogLevel level, const char *file, int line, const char *fmt, ...)
    {
        // Early-out: level check (should already be checked by macro, but be safe)
        if (level < min_level_.load(std::memory_order_relaxed))
            return;

        if (!check_rate_limit())
            return;

        // Format timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

        struct tm tm_buf{};
        localtime_r(&time_t_now, &tm_buf);

        // Get thread id
        auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id()) % 100000;

        // Strip path to filename only
        const char *filename = file;
        for (const char *p = file; *p; ++p)
        {
            if (*p == '/' || *p == '\\')
                filename = p + 1;
        }

        // Build the full log line
        char header[192];
        std::snprintf(header, sizeof(header),
                      "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] [%05lu] [%s:%d] ",
                      tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                      tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                      static_cast<int>(ms.count()),
                      log_level_name(level),
                      static_cast<unsigned long>(tid),
                      filename, line);

        char message[1024];
        va_list args;
        va_start(args, fmt);
        int mlen = std::vsnprintf(message, sizeof(message), fmt, args);
        va_end(args);
        if (mlen < 0)
            mlen = 0;
        if (mlen >= static_cast<int>(sizeof(message)))
            mlen = static_cast<int>(sizeof(message)) - 1;

        // Combine header + message + newline
        char buf[1280];
        int total = std::snprintf(buf, sizeof(buf), "%s%s\n", header, message);
        if (total >= static_cast<int>(sizeof(buf)))
            total = static_cast<int>(sizeof(buf)) - 1;

        write_line(buf, total, level);
    }

    void Logger::write_line(const char *buf, int len, LogLevel level)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (console_enabled_.load(std::memory_order_relaxed))
        {
            FILE *out = (level >= LogLevel::Warn) ? stderr : stdout;
            std::fwrite(buf, 1, static_cast<size_t>(len), out);
        }

        if (file_)
        {
            std::fwrite(buf, 1, static_cast<size_t>(len), file_);
        }

        // Auto-flush on Error/Fatal; periodic flush otherwise
        if (level >= LogLevel::Error)
        {
            if (file_)
                std::fflush(file_);
            if (console_enabled_.load(std::memory_order_relaxed))
            {
                std::fflush(stdout);
                std::fflush(stderr);
            }
        }
        else
        {
            auto now = std::chrono::steady_clock::now();
            if (now - last_flush_ > std::chrono::seconds(1))
            {
                if (file_)
                    std::fflush(file_);
                last_flush_ = now;
            }
        }
    }

    void Logger::flush()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_)
            std::fflush(file_);
        std::fflush(stdout);
        std::fflush(stderr);
    }

} // namespace luft
