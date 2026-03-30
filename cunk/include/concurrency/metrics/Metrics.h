#pragma once

/// @file Metrics.h
/// @brief Lightweight instrumentation for concurrency framework components.
///
/// DESIGN:
///   Provides atomic counters, latency tracking, and throughput measurement.
///   Components (executor, queue, event loop) register metrics via a central
///   MetricsRegistry. External monitoring systems can poll the registry or
///   register a callback sink.
///
/// PERFORMANCE RATIONALE:
///   - All counters use relaxed atomics: we don't need ordering guarantees
///     for monitoring data, just eventual visibility. Relaxed atomics on x86
///     compile to plain MOV (no fence), so they're essentially free.
///   - Latency tracking uses a simple min/max/sum/count model rather than
///     histograms to minimize per-sample overhead. For percentiles, users
///     should use an external system (Prometheus, etc.) fed by the sink.
///   - MetricsSink is an abstract callback so the framework doesn't depend
///     on any particular monitoring library.

#include "../Common.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace conc
{

    /// @brief Thread-safe atomic counter.
    /// Uses relaxed ordering for maximum performance (no fence on x86).
    class Counter
    {
    public:
        Counter() = default;

        void increment(std::uint64_t n = 1)
        {
            value_.fetch_add(n, std::memory_order_relaxed);
        }

        std::uint64_t value() const
        {
            return value_.load(std::memory_order_relaxed);
        }

        void reset()
        {
            value_.store(0, std::memory_order_relaxed);
        }

    private:
        alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> value_{0};
    };

    /// @brief Thread-safe gauge (can go up and down).
    class Gauge
    {
    public:
        Gauge() = default;

        void set(std::int64_t v)
        {
            value_.store(v, std::memory_order_relaxed);
        }

        void increment(std::int64_t n = 1)
        {
            value_.fetch_add(n, std::memory_order_relaxed);
        }

        void decrement(std::int64_t n = 1)
        {
            value_.fetch_sub(n, std::memory_order_relaxed);
        }

        std::int64_t value() const
        {
            return value_.load(std::memory_order_relaxed);
        }

    private:
        alignas(CACHE_LINE_SIZE) std::atomic<std::int64_t> value_{0};
    };

    /// @brief Latency tracker (min/max/sum/count).
    /// Thread-safe for concurrent record() calls.
    class LatencyTracker
    {
    public:
        using Duration = std::chrono::nanoseconds;

        LatencyTracker() = default;

        /// @brief Record a latency sample.
        void record(Duration d)
        {
            auto ns = d.count();
            count_.fetch_add(1, std::memory_order_relaxed);
            sum_ns_.fetch_add(ns, std::memory_order_relaxed);

            // Update min (lock-free CAS loop)
            auto cur_min = min_ns_.load(std::memory_order_relaxed);
            while (ns < cur_min && !min_ns_.compare_exchange_weak(
                                       cur_min, ns, std::memory_order_relaxed))
            {
            }

            // Update max
            auto cur_max = max_ns_.load(std::memory_order_relaxed);
            while (ns > cur_max && !max_ns_.compare_exchange_weak(
                                       cur_max, ns, std::memory_order_relaxed))
            {
            }
        }

        /// @brief RAII scope timer. Records duration on destruction.
        class ScopeTimer
        {
        public:
            explicit ScopeTimer(LatencyTracker &tracker)
                : tracker_(tracker), start_(std::chrono::steady_clock::now()) {}

            ~ScopeTimer()
            {
                auto end = std::chrono::steady_clock::now();
                tracker_.record(std::chrono::duration_cast<Duration>(end - start_));
            }

            ScopeTimer(const ScopeTimer &) = delete;
            ScopeTimer &operator=(const ScopeTimer &) = delete;

        private:
            LatencyTracker &tracker_;
            std::chrono::steady_clock::time_point start_;
        };

        struct Snapshot
        {
            std::uint64_t count;
            std::int64_t min_ns;
            std::int64_t max_ns;
            std::int64_t sum_ns;
            double avg_ns() const { return count > 0 ? static_cast<double>(sum_ns) / count : 0.0; }
        };

        Snapshot snapshot() const
        {
            return {
                count_.load(std::memory_order_relaxed),
                min_ns_.load(std::memory_order_relaxed),
                max_ns_.load(std::memory_order_relaxed),
                sum_ns_.load(std::memory_order_relaxed)};
        }

        void reset()
        {
            count_.store(0, std::memory_order_relaxed);
            min_ns_.store(INT64_MAX, std::memory_order_relaxed);
            max_ns_.store(0, std::memory_order_relaxed);
            sum_ns_.store(0, std::memory_order_relaxed);
        }

    private:
        alignas(CACHE_LINE_SIZE) std::atomic<std::uint64_t> count_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<std::int64_t> min_ns_{INT64_MAX};
        alignas(CACHE_LINE_SIZE) std::atomic<std::int64_t> max_ns_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<std::int64_t> sum_ns_{0};
    };

    /// @brief Callback interface for exporting metrics to external systems.
    class MetricsSink
    {
    public:
        virtual ~MetricsSink() = default;

        virtual void on_counter(const std::string &name, std::uint64_t value) = 0;
        virtual void on_gauge(const std::string &name, std::int64_t value) = 0;
        virtual void on_latency(const std::string &name, const LatencyTracker::Snapshot &snap) = 0;
    };

    /// @brief Central registry for all framework metrics.
    ///
    /// Components register named metrics here. External systems poll or attach
    /// a sink to export data.
    ///
    /// Thread-safety: all methods are thread-safe.
    class MetricsRegistry
    {
    public:
        MetricsRegistry() = default;

        Counter &counter(const std::string &name)
        {
            std::lock_guard lock(mutex_);
            auto [it, _] = counters_.try_emplace(name, std::make_unique<Counter>());
            return *it->second;
        }

        Gauge &gauge(const std::string &name)
        {
            std::lock_guard lock(mutex_);
            auto [it, _] = gauges_.try_emplace(name, std::make_unique<Gauge>());
            return *it->second;
        }

        LatencyTracker &latency(const std::string &name)
        {
            std::lock_guard lock(mutex_);
            auto [it, _] = latencies_.try_emplace(name, std::make_unique<LatencyTracker>());
            return *it->second;
        }

        /// @brief Export all metrics to a sink.
        void report(MetricsSink &sink) const
        {
            std::lock_guard lock(mutex_);
            for (auto &[name, c] : counters_)
            {
                sink.on_counter(name, c->value());
            }
            for (auto &[name, g] : gauges_)
            {
                sink.on_gauge(name, g->value());
            }
            for (auto &[name, l] : latencies_)
            {
                sink.on_latency(name, l->snapshot());
            }
        }

        /// @brief Get a snapshot of all metrics as strings (for simple logging).
        std::vector<std::pair<std::string, std::string>> dump() const
        {
            std::vector<std::pair<std::string, std::string>> result;
            std::lock_guard lock(mutex_);
            for (auto &[name, c] : counters_)
            {
                result.emplace_back(name, std::to_string(c->value()));
            }
            for (auto &[name, g] : gauges_)
            {
                result.emplace_back(name, std::to_string(g->value()));
            }
            for (auto &[name, l] : latencies_)
            {
                auto s = l->snapshot();
                result.emplace_back(name + ".count", std::to_string(s.count));
                result.emplace_back(name + ".avg_ns", std::to_string(static_cast<std::int64_t>(s.avg_ns())));
                result.emplace_back(name + ".min_ns", std::to_string(s.min_ns));
                result.emplace_back(name + ".max_ns", std::to_string(s.max_ns));
            }
            return result;
        }

    private:
        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::unique_ptr<Counter>> counters_;
        std::unordered_map<std::string, std::unique_ptr<Gauge>> gauges_;
        std::unordered_map<std::string, std::unique_ptr<LatencyTracker>> latencies_;
    };

} // namespace conc
