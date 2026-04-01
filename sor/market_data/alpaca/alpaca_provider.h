#pragma once

// Alpaca Markets real-time data provider.
// Connects to the Alpaca V2 WebSocket streaming API for quote data,
// normalizes messages into OrderBook updates, and pushes them into
// the MarketDataAggregator.

#ifdef SOR_HAS_LIVE_FEED

#include "market_data/provider.h"
#include "market_data/feed_quality_monitor.h"
#include "market_data/alpaca/alpaca_normalizer.h"
#include "market_data/aggregator.h"

#include <ixwebsocket/IXWebSocket.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace sor::market_data::alpaca
{

    class AlpacaProvider : public MarketDataProvider
    {
    public:
        struct Config
        {
            std::string api_key;
            std::string api_secret;
            std::string ws_url{"wss://stream.data.alpaca.markets/v2/iex"};
            VenueId venue_id{100};
            std::chrono::seconds reconnect_delay{5};
            int max_reconnect_attempts{10};
        };

        explicit AlpacaProvider(Config config);
        ~AlpacaProvider() override;

        // MarketDataProvider interface
        bool connect() override;
        void disconnect() override;
        [[nodiscard]] bool is_connected() const override;
        void subscribe(const Symbol &symbol) override;
        void unsubscribe(const Symbol &symbol) override;
        void set_aggregator(MarketDataAggregator &aggregator) override;
        [[nodiscard]] ProviderType type() const noexcept override { return ProviderType::Alpaca; }
        [[nodiscard]] const char *name() const noexcept override { return "Alpaca"; }
        [[nodiscard]] const FeedQualityMonitor *quality_monitor() const override { return &quality_; }

        [[nodiscard]] VenueId venue_id() const noexcept { return config_.venue_id; }

    private:
        void on_ws_message(const ix::WebSocketMessagePtr &msg);
        void send_auth();
        void send_subscriptions();

        Config config_;
        MarketDataAggregator *aggregator_{nullptr};
        AlpacaNormalizer normalizer_;
        FeedQualityMonitor quality_;

        ix::WebSocket ws_;
        std::atomic<bool> connected_{false};
        std::atomic<bool> authenticated_{false};
        std::atomic<bool> should_run_{false};

        std::mutex sub_mutex_;
        std::unordered_set<std::string> pending_symbols_;
        std::unordered_set<std::string> subscribed_symbols_;
    };

} // namespace sor::market_data::alpaca

#endif // SOR_HAS_LIVE_FEED
