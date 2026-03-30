#pragma once

// VWAP (Volume Weighted Average Price) routing strategy.
//
// Time-slices a parent order into child orders spread across a target
// duration.  Each slice is routed to the best venue via BestPrice logic.
// The strategy tracks execution progress and dynamically adjusts its
// pace: accelerating when behind schedule and throttling when ahead.

#include "routing/strategy.h"
#include <chrono>

namespace sor::routing
{

    class VWAPStrategy final : public RoutingStrategy
    {
    public:
        // -----------------------------------------------------------------------
        // Configuration
        // -----------------------------------------------------------------------

        struct Config
        {
            /// Target execution duration.
            std::chrono::microseconds duration{std::chrono::minutes(30)};

            /// Number of equal time slices.
            uint32_t num_slices{30};

            /// Urgency factor [0.0, 1.0].
            /// 0.0 = fully passive (never over-participate).
            /// 1.0 = fully aggressive (complete as fast as possible).
            double urgency{0.5};

            /// Maximum participation rate as fraction of expected slice volume.
            /// E.g., 0.25 = never send more than 25% of a slice's expected volume
            /// in a single child order.
            double max_participation_rate{0.25};
        };

        VWAPStrategy() noexcept;
        explicit VWAPStrategy(const Config &cfg) noexcept;

        // -----------------------------------------------------------------------
        // RoutingStrategy interface
        // -----------------------------------------------------------------------

        /// Standard route() -- returns the next child order slice based on
        /// elapsed time and fill progress.
        RoutingDecision route(const Order &order,
                              const market_data::NBBO &nbbo,
                              const market_data::AggregatedBook &book,
                              const std::vector<VenueScore> &venues) override;

        const char *name() const noexcept override { return "VWAP"; }
        sor::RoutingStrategy type() const noexcept override
        {
            return sor::RoutingStrategy::VWAP;
        }

        // -----------------------------------------------------------------------
        // VWAP-specific interface
        // -----------------------------------------------------------------------

        /// Initialize / reset state for a new parent order.
        void begin(Quantity total_quantity, Timestamp start_time);

        /// Report fills so the strategy can track progress.
        void on_fill(Quantity filled_qty) noexcept;

        /// Get the next child-order slice based on time and fill progress.
        /// Returns an empty decision if we should wait.
        RoutingDecision get_next_slice(const Order &order,
                                       const market_data::NBBO &nbbo,
                                       const market_data::AggregatedBook &book,
                                       const std::vector<VenueScore> &venues);

        /// Fraction of total quantity filled so far [0, 1].
        [[nodiscard]] double fill_progress() const noexcept;

        /// Fraction of time elapsed [0, 1].
        [[nodiscard]] double time_progress() const noexcept;

        /// Current participation rate relative to schedule.
        [[nodiscard]] double participation_rate() const noexcept;

        /// True when the strategy considers itself complete.
        [[nodiscard]] bool is_complete() const noexcept;

        void set_config(const Config &cfg) noexcept { config_ = cfg; }
        [[nodiscard]] const Config &config() const noexcept { return config_; }

    private:
        /// Compute the target quantity for the current time slice, adjusted for
        /// urgency and fill progress.
        [[nodiscard]] Quantity compute_slice_quantity() const noexcept;

        Config config_;

        // Execution state.
        Quantity total_quantity_{0};
        Quantity filled_so_far_{0};
        Timestamp start_time_{};
        Timestamp end_time_{};
        uint32_t slices_sent_{0};
    };

} // namespace sor::routing
