#include "infra/metrics.h"
#include "infra/logging.h"

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>

#include <utility>

namespace sor::infra
{

    // ---------------------------------------------------------------------------
    // Prometheus implementation detail
    // ---------------------------------------------------------------------------

    struct MetricsManager::Impl
    {
        std::shared_ptr<prometheus::Registry> registry;
        std::unique_ptr<prometheus::Exposer> exposer;

        // Pre-built metric families for the SOR-specific helpers.
        prometheus::Family<prometheus::Histogram> *order_latency_family{nullptr};
        prometheus::Family<prometheus::Histogram> *routing_latency_family{nullptr};
        prometheus::Family<prometheus::Histogram> *venue_latency_family{nullptr};
        prometheus::Family<prometheus::Counter> *orders_routed_family{nullptr};
        prometheus::Family<prometheus::Counter> *orders_rejected_family{nullptr};
        prometheus::Family<prometheus::Counter> *fills_family{nullptr};
        prometheus::Family<prometheus::Gauge> *active_orders_family{nullptr};
        prometheus::Family<prometheus::Gauge> *position_family{nullptr};

        // Individual metrics (label-free).
        prometheus::Histogram *order_latency{nullptr};
        prometheus::Histogram *routing_latency{nullptr};
        prometheus::Counter *orders_routed{nullptr};
        prometheus::Counter *orders_rejected{nullptr};
        prometheus::Counter *fills{nullptr};
        prometheus::Gauge *active_orders{nullptr};
    };

    // ---------------------------------------------------------------------------
    // Singleton
    // ---------------------------------------------------------------------------

    MetricsManager &MetricsManager::instance()
    {
        static MetricsManager inst;
        return inst;
    }

    // ---------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------

    bool MetricsManager::init(int port)
    {
        if (initialized_.load(std::memory_order_relaxed))
        {
            return true; // Already initialised.
        }

        try
        {
            impl_ = std::make_unique<Impl>();
            impl_->registry = std::make_shared<prometheus::Registry>();

            // Latency histograms use microsecond buckets.
            const auto latency_buckets = prometheus::Histogram::BucketBoundaries{
                1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 50000, 100000};

            // -- Order latency --
            impl_->order_latency_family = &prometheus::BuildHistogram()
                                               .Name("sor_order_latency_us")
                                               .Help("End-to-end order latency in microseconds")
                                               .Register(*impl_->registry);
            impl_->order_latency = &impl_->order_latency_family->Add({}, latency_buckets);

            // -- Routing latency --
            impl_->routing_latency_family = &prometheus::BuildHistogram()
                                                 .Name("sor_routing_latency_us")
                                                 .Help("Routing decision latency in microseconds")
                                                 .Register(*impl_->registry);
            impl_->routing_latency = &impl_->routing_latency_family->Add({}, latency_buckets);

            // -- Venue latency (per-venue label applied dynamically) --
            impl_->venue_latency_family = &prometheus::BuildHistogram()
                                               .Name("sor_venue_latency_us")
                                               .Help("Per-venue round-trip latency in microseconds")
                                               .Register(*impl_->registry);

            // -- Counters --
            impl_->orders_routed_family = &prometheus::BuildCounter()
                                               .Name("sor_orders_routed_total")
                                               .Help("Total orders routed")
                                               .Register(*impl_->registry);
            impl_->orders_routed = &impl_->orders_routed_family->Add({});

            impl_->orders_rejected_family = &prometheus::BuildCounter()
                                                 .Name("sor_orders_rejected_total")
                                                 .Help("Total orders rejected by risk checks")
                                                 .Register(*impl_->registry);
            impl_->orders_rejected = &impl_->orders_rejected_family->Add({});

            impl_->fills_family = &prometheus::BuildCounter()
                                       .Name("sor_fills_total")
                                       .Help("Total fills received")
                                       .Register(*impl_->registry);
            impl_->fills = &impl_->fills_family->Add({});

            // -- Gauges --
            impl_->active_orders_family = &prometheus::BuildGauge()
                                               .Name("sor_active_orders")
                                               .Help("Current number of active orders")
                                               .Register(*impl_->registry);
            impl_->active_orders = &impl_->active_orders_family->Add({});

            impl_->position_family = &prometheus::BuildGauge()
                                          .Name("sor_position")
                                          .Help("Current position per symbol")
                                          .Register(*impl_->registry);

            // -- HTTP endpoint --
            impl_->exposer = std::make_unique<prometheus::Exposer>(
                "0.0.0.0:" + std::to_string(port));
            impl_->exposer->RegisterCollectable(impl_->registry);

            initialized_.store(true, std::memory_order_release);
            SOR_LOG_INFO("Metrics endpoint started on port {}", port);
            return true;
        }
        catch (const std::exception &e)
        {
            SOR_LOG_ERROR("Failed to initialise metrics: {}", e.what());
            impl_.reset();
            return false;
        }
    }

    void MetricsManager::shutdown()
    {
        if (!initialized_.load(std::memory_order_relaxed))
        {
            return;
        }
        impl_.reset();
        initialized_.store(false, std::memory_order_release);
        SOR_LOG_INFO("Metrics endpoint shut down");
    }

    // ---------------------------------------------------------------------------
    // Generic metric operations
    // ---------------------------------------------------------------------------

    MetricsManager::SimpleMetric &MetricsManager::get_or_create(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        auto it = metrics_.find(name);
        if (it != metrics_.end())
        {
            return *it->second;
        }
        auto [inserted, _] = metrics_.emplace(name, std::make_unique<SimpleMetric>(name));
        return *inserted->second;
    }

    void MetricsManager::increment(const std::string &name, double value)
    {
        auto &m = get_or_create(name);
        // Atomic add via CAS loop (double has no fetch_add).
        double old_val = m.value.load(std::memory_order_relaxed);
        while (!m.value.compare_exchange_weak(old_val, old_val + value,
                                              std::memory_order_release,
                                              std::memory_order_relaxed))
        {
            // old_val is updated by compare_exchange_weak on failure.
        }
    }

    void MetricsManager::set_gauge(const std::string &name, double value)
    {
        auto &m = get_or_create(name);
        m.value.store(value, std::memory_order_release);
    }

    void MetricsManager::observe(const std::string &name, double value)
    {
        // For the simple fallback path, just accumulate into a gauge.
        // The prometheus path records into a real histogram.
        auto &m = get_or_create(name);
        double old_val = m.value.load(std::memory_order_relaxed);
        while (!m.value.compare_exchange_weak(old_val, old_val + value,
                                              std::memory_order_release,
                                              std::memory_order_relaxed))
        {
        }
    }

    // ---------------------------------------------------------------------------
    // ScopedTimer
    // ---------------------------------------------------------------------------

    MetricsManager::ScopedTimer::ScopedTimer(const std::string &metric_name)
        : name_(metric_name), start_(std::chrono::steady_clock::now()) {}

    MetricsManager::ScopedTimer::~ScopedTimer()
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_);
        MetricsManager::instance().observe(name_, static_cast<double>(elapsed.count()));
    }

    // ---------------------------------------------------------------------------
    // Pre-defined SOR metrics
    // ---------------------------------------------------------------------------

    void MetricsManager::record_order_latency(double microseconds)
    {
        if (impl_ && impl_->order_latency)
        {
            impl_->order_latency->Observe(microseconds);
        }
        observe("sor_order_latency_us", microseconds);
    }

    void MetricsManager::record_routing_latency(double microseconds)
    {
        if (impl_ && impl_->routing_latency)
        {
            impl_->routing_latency->Observe(microseconds);
        }
        observe("sor_routing_latency_us", microseconds);
    }

    void MetricsManager::record_venue_latency(const std::string &venue, double microseconds)
    {
        if (impl_ && impl_->venue_latency_family)
        {
            impl_->venue_latency_family->Add({{"venue", venue}},
                                             prometheus::Histogram::BucketBoundaries{
                                                 1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 50000, 100000})
                .Observe(microseconds);
        }
        observe("sor_venue_latency_us." + venue, microseconds);
    }

    void MetricsManager::increment_orders_routed()
    {
        if (impl_ && impl_->orders_routed)
        {
            impl_->orders_routed->Increment();
        }
        increment("sor_orders_routed_total");
    }

    void MetricsManager::increment_orders_rejected()
    {
        if (impl_ && impl_->orders_rejected)
        {
            impl_->orders_rejected->Increment();
        }
        increment("sor_orders_rejected_total");
    }

    void MetricsManager::increment_fills()
    {
        if (impl_ && impl_->fills)
        {
            impl_->fills->Increment();
        }
        increment("sor_fills_total");
    }

    void MetricsManager::set_active_orders(int64_t count)
    {
        if (impl_ && impl_->active_orders)
        {
            impl_->active_orders->Set(static_cast<double>(count));
        }
        set_gauge("sor_active_orders", static_cast<double>(count));
    }

    void MetricsManager::set_position(const std::string &symbol, double quantity)
    {
        if (impl_ && impl_->position_family)
        {
            impl_->position_family->Add({{"symbol", symbol}}).Set(quantity);
        }
        set_gauge("sor_position." + symbol, quantity);
    }

} // namespace sor::infra
