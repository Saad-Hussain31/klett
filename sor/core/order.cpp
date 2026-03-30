#include "core/order.h"

namespace sor
{

    bool Order::is_terminal() const noexcept
    {
        switch (state)
        {
        case OrderState::Filled:
        case OrderState::Canceled:
        case OrderState::Rejected:
        case OrderState::Expired:
            return true;
        default:
            return false;
        }
    }

    bool Order::is_active() const noexcept
    {
        switch (state)
        {
        case OrderState::New:
        case OrderState::PendingNew:
        case OrderState::Accepted:
        case OrderState::PartiallyFilled:
        case OrderState::PendingCancel:
        case OrderState::PendingReplace:
            return true;
        default:
            return false;
        }
    }

} // namespace sor
