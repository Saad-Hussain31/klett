#include "market_data/provider_factory.h"
#include "infra/logging.h"

#ifdef SOR_HAS_LIVE_FEED
#include "market_data/alpaca/alpaca_provider.h"
#endif

namespace sor::market_data
{

    std::unique_ptr<MarketDataProvider> create_provider(
        const infra::MarketDataConfig &config)
    {
        if (config.provider == "simulated")
        {
            // Caller uses the existing SimulatedFeedHandler path
            return nullptr;
        }

#ifdef SOR_HAS_LIVE_FEED
        if (config.provider == "alpaca")
        {
            alpaca::AlpacaProvider::Config ac;
            ac.api_key = config.alpaca_api_key;
            ac.api_secret = config.alpaca_api_secret;
            ac.ws_url = config.alpaca_ws_url;
            ac.venue_id = 100; // Alpaca as a single virtual venue
            ac.reconnect_delay = std::chrono::seconds(config.reconnect_delay_sec);
            ac.max_reconnect_attempts = config.max_reconnect_attempts;
            return std::make_unique<alpaca::AlpacaProvider>(std::move(ac));
        }
#endif

        SOR_LOG_ERROR("[ProviderFactory] Unknown provider type: {}", config.provider);
        return nullptr;
    }

} // namespace sor::market_data
