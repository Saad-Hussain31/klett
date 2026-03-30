#include "infra/logging.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <vector>

namespace sor::infra
{

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    namespace
    {

        spdlog::level::level_enum to_spdlog_level(LogLevel level) noexcept
        {
            switch (level)
            {
            case LogLevel::Trace:
                return spdlog::level::trace;
            case LogLevel::Debug:
                return spdlog::level::debug;
            case LogLevel::Info:
                return spdlog::level::info;
            case LogLevel::Warn:
                return spdlog::level::warn;
            case LogLevel::Error:
                return spdlog::level::err;
            case LogLevel::Critical:
                return spdlog::level::critical;
            }
            return spdlog::level::info; // unreachable, but silences warnings
        }

    } // anonymous namespace

    // ---------------------------------------------------------------------------
    // Singleton
    // ---------------------------------------------------------------------------

    Logger &Logger::instance()
    {
        static Logger inst;
        return inst;
    }

    // ---------------------------------------------------------------------------
    // Initialisation
    // ---------------------------------------------------------------------------

    void Logger::init(const std::string &name,
                      const std::string &log_file,
                      LogLevel level)
    {
        // Build sink list: always add colour console.
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

        // Optionally add a 50 MB rotating file sink with 5 backup files.
        if (!log_file.empty())
        {
            constexpr std::size_t max_file_size = 50 * 1024 * 1024; // 50 MB
            constexpr std::size_t max_files = 5;
            sinks.push_back(
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    log_file, max_file_size, max_files));
        }

        logger_ = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());

        // "[2024-03-30 14:05:23.123456] [info] [12345] order routed to venue A"
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] [%t] %v");
        logger_->set_level(to_spdlog_level(level));

        // Flush on warn and above so critical messages are never lost.
        logger_->flush_on(spdlog::level::warn);

        // Register as the default spdlog logger for convenience.
        spdlog::set_default_logger(logger_);
    }

    // ---------------------------------------------------------------------------
    // Runtime level change
    // ---------------------------------------------------------------------------

    void Logger::set_level(LogLevel level)
    {
        if (logger_)
        {
            logger_->set_level(to_spdlog_level(level));
        }
    }

    // ---------------------------------------------------------------------------
    // Accessor
    // ---------------------------------------------------------------------------

    std::shared_ptr<spdlog::logger> Logger::get() const
    {
        return logger_;
    }

} // namespace sor::infra
