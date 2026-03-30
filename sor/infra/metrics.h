#pragma once

// Prometheus-style metrics for the Smart Order Router.
//
// Provides counters, gauges, and histograms with a simple API.
// Falls back to lock-free atomic storage when prometheus-cpp is not linked.
// When prometheus-cpp IS available, an HTTP /metrics endpoint is exposed.

#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace sor::infra
{

    class MetricsManager
    {
    public:
        static MetricsManager &instance();

        /// Initialise the metrics system.
        /// @param port  HTTP port for the Prometheus pull endpoint.
        /// @returns true on success.
        bool init(int port = 9090);

        /// Shut down the HTTP endpoint and release resources.
        void shutdown();

        // ---- Generic metric operations ----------------------------------------

        /// Increment a counter by @p value (default 1.0).
        void increment(const std::string &name, double value = 1.0);

        /// Set an absolute gauge value.
        void set_gauge(const std::string &name, double value);

        /// Record an observation into a histogram bucket.
        void observe(const std::string &name, double value);

        // ---- Scoped timer (RAII) ----------------------------------------------

        /// Records elapsed wall-time in microseconds into the named histogram
        /// upon destruction.
        class ScopedTimer
        {
        public:
            explicit ScopedTimer(const std::string &metric_name);
            ~ScopedTimer();

            ScopedTimer(const ScopedTimer &) = delete;
            ScopedTimer &operator=(const ScopedTimer &) = delete;
            ScopedTimer(ScopedTimer &&) = delete;
            ScopedTimer &operator=(ScopedTimer &&) = delete;

        private:
            std::string name_;
            std::chrono::steady_clock::time_point start_;
        };

        // ---- Pre-defined SOR metrics ------------------------------------------

        void record_order_latency(double microseconds);
        void record_routing_latency(double microseconds);
        void record_venue_latency(const std::string &venue, double microseconds);
        void increment_orders_routed();
        void increment_orders_rejected();
        void increment_fills();
        void set_active_orders(int64_t count);
        void set_position(const std::string &symbol, double quantity);

        [[nodiscard]] bool is_initialized() const noexcept { return initialized_.load(std::memory_order_relaxed); }

    private:
        MetricsManager() = default;
        ~MetricsManager() = default;
        MetricsManager(const MetricsManager &) = delete;
        MetricsManager &operator=(const MetricsManager &) = delete;

        std::atomic<bool> initialized_{false};

        // ------ Internal simple-metric storage ---------------------------------
        // Each named metric is stored as an atomic double.  For production use,
        // these map 1-to-1 onto prometheus-cpp Family<Counter/Gauge/Histogram>
        // when that library is linked.

        struct SimpleMetric
        {
            std::string name;
            std::atomic<double> value{0.0};

            explicit SimpleMetric(std::string n) : name(std::move(n)) {}
        };

        /// Get-or-create a SimpleMetric by name.
        SimpleMetric &get_or_create(const std::string &name);

        std::unordered_map<std::string, std::unique_ptr<SimpleMetric>> metrics_;
        std::mutex metrics_mutex_;

        // ------ Prometheus integration (compile-time optional) ------------------
        // Actual prometheus-cpp exposer handle, kept as opaque pointer so the
        // header does not pull in prometheus headers.
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace sor::infra

// ---------------------------------------------------------------------------
// Convenience macro -- creates a ScopedTimer whose variable name is unique.
// ---------------------------------------------------------------------------
#define SOR_METRICS_TIMER_CONCAT_(a, b) a##b
#define SOR_METRICS_TIMER_CONCAT(a, b) SOR_METRICS_TIMER_CONCAT_(a, b)
#define SOR_METRICS_TIMER(name) \
    ::sor::infra::MetricsManager::ScopedTimer SOR_METRICS_TIMER_CONCAT(_sor_timer_, __LINE__)(name)
