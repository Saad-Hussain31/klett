#pragma once

// Structured logging wrapper around spdlog for the Smart Order Router.
// Thread-safe singleton with console + optional file sink.

#include <string>
#include <memory>
#include <cstdint>

// Include spdlog logger so that macro call-sites can invoke methods on the
// returned shared_ptr<spdlog::logger> without an incomplete-type error.
#include <spdlog/logger.h>

namespace sor::infra
{

    enum class LogLevel : uint8_t
    {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Critical
    };

    class Logger
    {
    public:
        /// Access the process-wide singleton.
        static Logger &instance();

        /// Initialise the logger.  Must be called once before any SOR_LOG_* macro.
        /// @param name       Logger name (appears in structured output).
        /// @param log_file   Optional path to a rotating log file.
        /// @param level      Minimum log level to emit.
        void init(const std::string &name,
                  const std::string &log_file = "",
                  LogLevel level = LogLevel::Info);

        /// Change the minimum log level at runtime.
        void set_level(LogLevel level);

        /// Get the underlying spdlog logger for direct use with SOR_LOG macros.
        /// Returns nullptr if init() has not been called.
        [[nodiscard]] std::shared_ptr<spdlog::logger> get() const;

    private:
        Logger() = default;
        ~Logger() = default;
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;

        std::shared_ptr<spdlog::logger> logger_;
    };

} // namespace sor::infra

// ---------------------------------------------------------------------------
// Convenience macros -- each acquires the shared_ptr once, so the branch is
// essentially free on the fast path (shared_ptr operator bool).
// ---------------------------------------------------------------------------
#define SOR_LOG_TRACE(...)                                   \
    do                                                       \
    {                                                        \
        if (auto l = ::sor::infra::Logger::instance().get()) \
            l->trace(__VA_ARGS__);                           \
    } while (0)
#define SOR_LOG_DEBUG(...)                                   \
    do                                                       \
    {                                                        \
        if (auto l = ::sor::infra::Logger::instance().get()) \
            l->debug(__VA_ARGS__);                           \
    } while (0)
#define SOR_LOG_INFO(...)                                    \
    do                                                       \
    {                                                        \
        if (auto l = ::sor::infra::Logger::instance().get()) \
            l->info(__VA_ARGS__);                            \
    } while (0)
#define SOR_LOG_WARN(...)                                    \
    do                                                       \
    {                                                        \
        if (auto l = ::sor::infra::Logger::instance().get()) \
            l->warn(__VA_ARGS__);                            \
    } while (0)
#define SOR_LOG_ERROR(...)                                   \
    do                                                       \
    {                                                        \
        if (auto l = ::sor::infra::Logger::instance().get()) \
            l->error(__VA_ARGS__);                           \
    } while (0)
#define SOR_LOG_CRITICAL(...)                                \
    do                                                       \
    {                                                        \
        if (auto l = ::sor::infra::Logger::instance().get()) \
            l->critical(__VA_ARGS__);                        \
    } while (0)
