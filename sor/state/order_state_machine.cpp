#include "state/order_state_machine.h"
#include <chrono>

namespace sor::state {

// ---------------------------------------------------------------------------
// Public API -- all lookups go through the constexpr transition table.
// ---------------------------------------------------------------------------

bool OrderStateMachine::is_valid_transition(OrderState from, OrderEvent event) noexcept
{
    const auto fi = static_cast<uint8_t>(from);
    const auto ei = static_cast<uint8_t>(event);
    if (fi >= NUM_ORDER_STATES || ei >= NUM_ORDER_EVENTS) [[unlikely]]
        return false;
    return detail::TRANSITION_TABLE[fi][ei] != INVALID_STATE;
}

std::optional<OrderState> OrderStateMachine::apply(OrderState current, OrderEvent event) noexcept
{
    const auto fi = static_cast<uint8_t>(current);
    const auto ei = static_cast<uint8_t>(event);
    if (fi >= NUM_ORDER_STATES || ei >= NUM_ORDER_EVENTS) [[unlikely]]
        return std::nullopt;

    const auto next = detail::TRANSITION_TABLE[fi][ei];
    if (next == INVALID_STATE) [[unlikely]]
        return std::nullopt;
    return next;
}

bool OrderStateMachine::apply(Order& order, OrderEvent event) noexcept
{
    auto result = apply(order.state, event);
    if (!result) [[unlikely]]
        return false;

    order.state = *result;
    ++order.version;
    order.last_update_time = std::chrono::steady_clock::now();
    return true;
}

std::vector<OrderEvent> OrderStateMachine::valid_events(OrderState state)
{
    std::vector<OrderEvent> events;
    events.reserve(NUM_ORDER_EVENTS);

    const auto si = static_cast<uint8_t>(state);
    if (si >= NUM_ORDER_STATES)
        return events;

    for (std::size_t e = 0; e < NUM_ORDER_EVENTS; ++e) {
        if (detail::TRANSITION_TABLE[si][e] != INVALID_STATE)
            events.push_back(static_cast<OrderEvent>(e));
    }
    return events;
}

// ---------------------------------------------------------------------------
// String conversion (lookup tables to avoid branching).
// ---------------------------------------------------------------------------

const char* OrderStateMachine::to_string(OrderState state) noexcept
{
    static constexpr const char* names[] = {
        "New",
        "PendingNew",
        "Accepted",
        "PartiallyFilled",
        "Filled",
        "PendingCancel",
        "Canceled",
        "Rejected",
        "Expired",
        "PendingReplace",
    };
    const auto idx = static_cast<uint8_t>(state);
    if (idx < NUM_ORDER_STATES)
        return names[idx];
    return "Unknown";
}

const char* OrderStateMachine::to_string(OrderEvent event) noexcept
{
    static constexpr const char* names[] = {
        "Submit",
        "Acknowledge",
        "PartialFill",
        "Fill",
        "RequestCancel",
        "CancelAck",
        "Reject",
        "Expire",
        "RequestReplace",
        "ReplaceAck",
    };
    const auto idx = static_cast<uint8_t>(event);
    if (idx < NUM_ORDER_EVENTS)
        return names[idx];
    return "Unknown";
}

} // namespace sor::state
