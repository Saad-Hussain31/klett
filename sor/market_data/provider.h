#pragma once

// Provider-agnostic market data interface.
// Concrete implementations (Alpaca, Replay, etc.) subscribe to symbols
// and push book updates into a MarketDataAggregator.

#include "core/types.h"

#include <cstdint>

namespace sor::market_data
{

    class MarketDataAggregator;
    class FeedQualityMonitor;

    enum class ProviderType : uint8_t
    {
        Simulated = 0,
        Alpaca = 1,
        Replay = 2,
    };

    class MarketDataProvider
    {
    public:
        virtual ~MarketDataProvider() = default;

        // Lifecycle
        virtual bool connect() = 0;
        virtual void disconnect() = 0;
        [[nodiscard]] virtual bool is_connected() const = 0;

        // Subscription management
        virtual void subscribe(const Symbol &symbol) = 0;
        virtual void unsubscribe(const Symbol &symbol) = 0;

        // The provider publishes updates through the aggregator
        virtual void set_aggregator(MarketDataAggregator &aggregator) = 0;

        // Provider metadata
        [[nodiscard]] virtual ProviderType type() const noexcept = 0;
        [[nodiscard]] virtual const char *name() const noexcept = 0;

        // Feed quality (optional, returns nullptr if not supported)
        [[nodiscard]] virtual const FeedQualityMonitor *quality_monitor() const { return nullptr; }
    };

} // namespace sor::market_data
