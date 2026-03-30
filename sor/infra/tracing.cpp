#include "infra/tracing.h"
#include "infra/logging.h"

#include <sstream>
#include <iomanip>
#include <algorithm>

namespace sor::infra
{

    // ---------------------------------------------------------------------------
    // Singleton
    // ---------------------------------------------------------------------------

    Tracer &Tracer::instance()
    {
        static Tracer inst;
        return inst;
    }

    // ---------------------------------------------------------------------------
    // Trace lifecycle
    // ---------------------------------------------------------------------------

    void Tracer::begin_trace(OrderId order_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &ot = traces_[order_id];
        ot.start = std::chrono::steady_clock::now();
        ot.active = true;
        ot.events.clear();

        TraceEvent ev;
        ev.order_id = order_id;
        ev.stage = "begin";
        ev.timestamp = ot.start;
        ev.latency_from_start = std::chrono::microseconds{0};
        ot.events.push_back(std::move(ev));
    }

    void Tracer::trace(OrderId order_id,
                       const std::string &stage,
                       const std::string &detail)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = traces_.find(order_id);
        if (it == traces_.end())
        {
            // If begin_trace was never called, silently start one now so we
            // never lose events.
            auto &ot = traces_[order_id];
            ot.start = std::chrono::steady_clock::now();
            ot.active = true;
            it = traces_.find(order_id);
        }

        auto &ot = it->second;
        const auto now = std::chrono::steady_clock::now();

        TraceEvent ev;
        ev.order_id = order_id;
        ev.stage = stage;
        ev.detail = detail;
        ev.timestamp = now;
        ev.latency_from_start = std::chrono::duration_cast<std::chrono::microseconds>(now - ot.start);
        ot.events.push_back(std::move(ev));
    }

    void Tracer::end_trace(OrderId order_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = traces_.find(order_id);
        if (it == traces_.end())
        {
            return;
        }

        auto &ot = it->second;
        const auto now = std::chrono::steady_clock::now();

        TraceEvent ev;
        ev.order_id = order_id;
        ev.stage = "end";
        ev.timestamp = now;
        ev.latency_from_start = std::chrono::duration_cast<std::chrono::microseconds>(now - ot.start);
        ot.events.push_back(std::move(ev));
        ot.active = false;
    }

    // ---------------------------------------------------------------------------
    // Query
    // ---------------------------------------------------------------------------

    std::vector<TraceEvent> Tracer::get_trace(OrderId order_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = traces_.find(order_id);
        if (it == traces_.end())
        {
            return {};
        }
        return it->second.events;
    }

    std::string Tracer::dump_trace(OrderId order_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = traces_.find(order_id);
        if (it == traces_.end())
        {
            return "(no trace for order " + std::to_string(order_id) + ")";
        }

        const auto &ot = it->second;
        std::ostringstream os;
        os << "Trace for order " << order_id
           << " (" << ot.events.size() << " events, "
           << (ot.active ? "active" : "completed") << "):\n";

        for (const auto &ev : ot.events)
        {
            os << "  [+" << std::setw(8) << ev.latency_from_start.count() << " us] "
               << ev.stage;
            if (!ev.detail.empty())
            {
                os << " -- " << ev.detail;
            }
            os << '\n';
        }
        return os.str();
    }

    bool Tracer::is_tracing(OrderId order_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = traces_.find(order_id);
        return it != traces_.end() && it->second.active;
    }

    // ---------------------------------------------------------------------------
    // Garbage collection
    // ---------------------------------------------------------------------------

    void Tracer::gc(std::size_t max_traces)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (traces_.size() <= max_traces)
        {
            return;
        }

        // Collect completed (inactive) traces sorted by their start time.
        // Remove the oldest completed traces first.
        struct Entry
        {
            OrderId id;
            Timestamp start;
        };

        std::vector<Entry> completed;
        completed.reserve(traces_.size());

        for (const auto &[id, ot] : traces_)
        {
            if (!ot.active)
            {
                completed.push_back({id, ot.start});
            }
        }

        // Sort oldest-first.
        std::sort(completed.begin(), completed.end(),
                  [](const Entry &a, const Entry &b)
                  { return a.start < b.start; });

        // Remove enough completed traces to get back under the limit.
        const std::size_t to_remove = traces_.size() - max_traces;
        std::size_t removed = 0;

        for (const auto &entry : completed)
        {
            if (removed >= to_remove)
            {
                break;
            }
            traces_.erase(entry.id);
            ++removed;
        }

        if (removed > 0)
        {
            SOR_LOG_DEBUG("Tracer GC: removed {} completed traces, {} remaining",
                          removed, traces_.size());
        }
    }

} // namespace sor::infra
