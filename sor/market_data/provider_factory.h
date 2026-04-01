#pragma once

// Factory function to create market data providers from config.

#include "market_data/provider.h"
#include "infra/config.h"

#include <memory>

namespace sor::market_data
{

    // Returns nullptr if provider type is "simulated" (caller should use
    // SimulatedFeedHandler directly). Returns a configured provider for
    // other types ("alpaca", etc.).
    std::unique_ptr<MarketDataProvider> create_provider(
        const infra::MarketDataConfig &config);

} // namespace sor::market_data
