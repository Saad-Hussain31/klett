#pragma once

#include "core/types.h"
#include <string>

namespace sor::ui
{

    struct OrderParams
    {
        std::string symbol{"AAPL"};
        Side side{Side::Buy};
        Quantity quantity{100};
        OrderType type{OrderType::Limit};
        double price{0.0}; // human-readable, converted to fixed-point on submit
        TimeInForce tif{TimeInForce::GTC};
        RoutingStrategy strategy{RoutingStrategy::BestPrice};
    };

} // namespace sor::ui
