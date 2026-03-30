#pragma once

// Order state machine with compile-time transition table.
// Enforces deterministic, FIX-protocol-aligned state transitions for all
// order lifecycle events. Invalid transitions are rejected at the API
// boundary rather than silently corrupting state.

#include "core/types.h"
#include "core/order.h"
#include <array>
#include <optional>
#include <vector>
#include <cstdint>

namespace sor::state {

// Sentinel value used in the transition table to mark disallowed transitions.
inline constexpr auto INVALID_STATE = static_cast<OrderState>(255);

inline constexpr std::size_t NUM_ORDER_STATES = 10;
inline constexpr std::size_t NUM_ORDER_EVENTS = 10;

// ---------------------------------------------------------------------------
// Events that trigger state transitions.
// Numeric values are used as column indices into the transition table.
// ---------------------------------------------------------------------------
enum class OrderEvent : uint8_t {
    Submit         = 0,   // Client submits  ->  New -> PendingNew
    Acknowledge    = 1,   // Venue acks      ->  PendingNew -> Accepted
    PartialFill    = 2,   // Partial fill    ->  Accepted/PartiallyFilled -> PartiallyFilled
    Fill           = 3,   // Full fill       ->  Accepted/PartiallyFilled -> Filled
    RequestCancel  = 4,   // Cancel request  ->  Accepted/PartiallyFilled -> PendingCancel
    CancelAck      = 5,   // Cancel ack      ->  PendingCancel -> Canceled
    Reject         = 6,   // Reject          ->  PendingNew -> Rejected; PendingCancel/PendingReplace -> Accepted
    Expire         = 7,   // Expiry          ->  Accepted/PartiallyFilled -> Expired
    RequestReplace = 8,   // Replace request ->  Accepted -> PendingReplace
    ReplaceAck     = 9,   // Replace ack     ->  PendingReplace -> Accepted
};

// ---------------------------------------------------------------------------
// Compile-time transition table.
//
// Every cell is initialised to INVALID_STATE, then the allowed transitions
// are written explicitly.  Any transition NOT listed here is rejected at
// run time.
// ---------------------------------------------------------------------------
namespace detail {

using TransitionTable =
    std::array<std::array<OrderState, NUM_ORDER_EVENTS>, NUM_ORDER_STATES>;

constexpr TransitionTable build_transition_table() noexcept
{
    TransitionTable t{};
    for (auto& row : t)
        for (auto& cell : row)
            cell = INVALID_STATE;

    using S = OrderState;
    using E = OrderEvent;

    auto set = [&](S from, E event, S to) {
        t[static_cast<uint8_t>(from)]
         [static_cast<uint8_t>(event)] = to;
    };

    // --- New ----------------------------------------------------------------
    set(S::New,             E::Submit,         S::PendingNew);

    // --- PendingNew ---------------------------------------------------------
    set(S::PendingNew,      E::Acknowledge,    S::Accepted);
    set(S::PendingNew,      E::Reject,         S::Rejected);

    // --- Accepted -----------------------------------------------------------
    set(S::Accepted,        E::PartialFill,    S::PartiallyFilled);
    set(S::Accepted,        E::Fill,           S::Filled);
    set(S::Accepted,        E::RequestCancel,  S::PendingCancel);
    set(S::Accepted,        E::Expire,         S::Expired);
    set(S::Accepted,        E::RequestReplace, S::PendingReplace);
    set(S::Accepted,        E::Reject,         S::Rejected); // late reject

    // --- PartiallyFilled ----------------------------------------------------
    set(S::PartiallyFilled, E::PartialFill,    S::PartiallyFilled);
    set(S::PartiallyFilled, E::Fill,           S::Filled);
    set(S::PartiallyFilled, E::RequestCancel,  S::PendingCancel);
    set(S::PartiallyFilled, E::Expire,         S::Expired);

    // --- PendingCancel ------------------------------------------------------
    // Exchange may fill the order before the cancel arrives.
    set(S::PendingCancel,   E::CancelAck,      S::Canceled);
    set(S::PendingCancel,   E::Fill,           S::Filled);
    // Partial fills while cancel is in-flight: stay PendingCancel.
    set(S::PendingCancel,   E::PartialFill,    S::PendingCancel);
    // Cancel reject: order is still live on the exchange.
    // Revert to Accepted; caller should check filled_quantity.
    set(S::PendingCancel,   E::Reject,         S::Accepted);

    // --- PendingReplace -----------------------------------------------------
    set(S::PendingReplace,  E::ReplaceAck,     S::Accepted);
    // Replace rejected: order unchanged on exchange, revert.
    set(S::PendingReplace,  E::Reject,         S::Accepted);
    // Exchange may fill while replace is in-flight.
    set(S::PendingReplace,  E::Fill,           S::Filled);

    return t;
}

inline constexpr TransitionTable TRANSITION_TABLE = build_transition_table();

} // namespace detail

// ---------------------------------------------------------------------------
// OrderStateMachine -- public API wrapping the constexpr table.
// ---------------------------------------------------------------------------
class OrderStateMachine {
public:
    // Returns true if the transition (from, event) is defined.
    [[nodiscard]] static bool is_valid_transition(OrderState from, OrderEvent event) noexcept;

    // Returns the target state, or std::nullopt when the transition is invalid.
    [[nodiscard]] static std::optional<OrderState> apply(OrderState current, OrderEvent event) noexcept;

    // Apply event directly to an Order, updating state and version.
    // Returns true on success, false (order unchanged) on invalid transition.
    static bool apply(Order& order, OrderEvent event) noexcept;

    // Enumerate every valid event from a given state (utility / debugging).
    [[nodiscard]] static std::vector<OrderEvent> valid_events(OrderState state);

    // Human-readable names (null-terminated string literals).
    [[nodiscard]] static const char* to_string(OrderState state) noexcept;
    [[nodiscard]] static const char* to_string(OrderEvent event) noexcept;
};

} // namespace sor::state
