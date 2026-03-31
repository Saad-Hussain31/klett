// Unit tests for OrderStateMachine -- every valid and invalid transition.

#include <catch2/catch_test_macros.hpp>

#include "state/order_state_machine.h"
#include "core/order.h"
#include "test_helpers.h"

using namespace sor;
using namespace sor::state;

// ---------------------------------------------------------------------------
// Helper: create an Order in a given state.
// ---------------------------------------------------------------------------
static Order make_order(OrderState state)
{
    Order o{};
    o.id = 1;
    o.symbol = "AAPL";
    o.side = Side::Buy;
    o.type = OrderType::Limit;
    o.tif = TimeInForce::GTC;
    o.price = to_price(150.0);
    o.quantity = 100;
    o.remaining_quantity = 100;
    o.state = state;
    o.version = 0;
    return o;
}

// ============================================================================
// Valid transitions
// ============================================================================

TEST_CASE("OSM: New -> PendingNew via Submit", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::New, OrderEvent::Submit);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::PendingNew);
    REQUIRE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::Submit));
}

TEST_CASE("OSM: PendingNew -> Accepted via Acknowledge", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PendingNew, OrderEvent::Acknowledge);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Accepted);
}

TEST_CASE("OSM: PendingNew -> Rejected via Reject", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PendingNew, OrderEvent::Reject);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Rejected);
}

TEST_CASE("OSM: Accepted -> PartiallyFilled via PartialFill", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::Accepted, OrderEvent::PartialFill);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::PartiallyFilled);
}

TEST_CASE("OSM: Accepted -> Filled via Fill", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::Accepted, OrderEvent::Fill);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Filled);
}

TEST_CASE("OSM: Accepted -> PendingCancel via RequestCancel", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::Accepted, OrderEvent::RequestCancel);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::PendingCancel);
}

TEST_CASE("OSM: Accepted -> Expired via Expire", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::Accepted, OrderEvent::Expire);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Expired);
}

TEST_CASE("OSM: Accepted -> PendingReplace via RequestReplace", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::Accepted, OrderEvent::RequestReplace);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::PendingReplace);
}

TEST_CASE("OSM: PartiallyFilled -> PartiallyFilled via PartialFill", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PartiallyFilled, OrderEvent::PartialFill);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::PartiallyFilled);
}

TEST_CASE("OSM: PartiallyFilled -> Filled via Fill", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PartiallyFilled, OrderEvent::Fill);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Filled);
}

TEST_CASE("OSM: PartiallyFilled -> PendingCancel via RequestCancel", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PartiallyFilled, OrderEvent::RequestCancel);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::PendingCancel);
}

TEST_CASE("OSM: PendingCancel -> Canceled via CancelAck", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PendingCancel, OrderEvent::CancelAck);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Canceled);
}

TEST_CASE("OSM: PendingCancel -> Filled via Fill (race)", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PendingCancel, OrderEvent::Fill);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Filled);
}

TEST_CASE("OSM: PendingCancel -> PendingCancel via PartialFill (in-flight)", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PendingCancel, OrderEvent::PartialFill);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::PendingCancel);
}

TEST_CASE("OSM: PendingCancel -> Accepted via Reject (cancel rejected)", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PendingCancel, OrderEvent::Reject);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Accepted);
}

TEST_CASE("OSM: PendingReplace -> Accepted via ReplaceAck", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PendingReplace, OrderEvent::ReplaceAck);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Accepted);
}

TEST_CASE("OSM: PendingReplace -> Accepted via Reject (replace rejected)", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PendingReplace, OrderEvent::Reject);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Accepted);
}

TEST_CASE("OSM: PendingReplace -> Filled via Fill (race)", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PendingReplace, OrderEvent::Fill);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Filled);
}

// ============================================================================
// Additional valid transitions from PartiallyFilled
// ============================================================================

TEST_CASE("OSM: PartiallyFilled -> Expired via Expire", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::PartiallyFilled, OrderEvent::Expire);
    REQUIRE(result.has_value());
    REQUIRE(*result == OrderState::Expired);
}

// ============================================================================
// Invalid transitions
// ============================================================================

TEST_CASE("OSM: New + Fill is invalid", "[state_machine]")
{
    auto result = StateTransitionLogger::apply(OrderState::New, OrderEvent::Fill);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::Fill));
}

TEST_CASE("OSM: New + any event besides Submit is invalid", "[state_machine]")
{
    REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::Acknowledge));
    REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::PartialFill));
    REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::Fill));
    REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::RequestCancel));
    REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::CancelAck));
    REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::Reject));
    REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::Expire));
    REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::RequestReplace));
    REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::New, OrderEvent::ReplaceAck));
    std::cout << "[STATE] Verified: New state rejects all events except Submit\n";
}

TEST_CASE("OSM: Filled is terminal -- all events fail", "[state_machine]")
{
    std::cout << "[STATE] Testing terminal state: Filled\n";
    for (uint8_t e = 0; e < static_cast<uint8_t>(NUM_ORDER_EVENTS); ++e)
    {
        auto event = static_cast<OrderEvent>(e);
        auto result = StateTransitionLogger::apply(OrderState::Filled, event);
        REQUIRE_FALSE(result.has_value());
        REQUIRE_FALSE(OrderStateMachine::is_valid_transition(OrderState::Filled, event));
    }
}

TEST_CASE("OSM: Canceled is terminal -- all events fail", "[state_machine]")
{
    std::cout << "[STATE] Testing terminal state: Canceled\n";
    for (uint8_t e = 0; e < static_cast<uint8_t>(NUM_ORDER_EVENTS); ++e)
    {
        auto event = static_cast<OrderEvent>(e);
        auto result = StateTransitionLogger::apply(OrderState::Canceled, event);
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("OSM: Rejected is terminal -- all events fail", "[state_machine]")
{
    std::cout << "[STATE] Testing terminal state: Rejected\n";
    for (uint8_t e = 0; e < static_cast<uint8_t>(NUM_ORDER_EVENTS); ++e)
    {
        auto event = static_cast<OrderEvent>(e);
        auto result = StateTransitionLogger::apply(OrderState::Rejected, event);
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("OSM: Expired is terminal -- all events fail", "[state_machine]")
{
    std::cout << "[STATE] Testing terminal state: Expired\n";
    for (uint8_t e = 0; e < static_cast<uint8_t>(NUM_ORDER_EVENTS); ++e)
    {
        auto event = static_cast<OrderEvent>(e);
        auto result = StateTransitionLogger::apply(OrderState::Expired, event);
        REQUIRE_FALSE(result.has_value());
    }
}

// ============================================================================
// Apply on Order object
// ============================================================================

TEST_CASE("OSM: apply on Order updates state and bumps version", "[state_machine]")
{
    Order o = make_order(OrderState::New);
    REQUIRE(o.version == 0);

    SECTION("Successful transition")
    {
        bool ok = StateTransitionLogger::apply(o, OrderEvent::Submit);
        REQUIRE(ok);
        REQUIRE(o.state == OrderState::PendingNew);
        REQUIRE(o.version == 1);
    }

    SECTION("Failed transition does not change order")
    {
        bool ok = StateTransitionLogger::apply(o, OrderEvent::Fill);
        REQUIRE_FALSE(ok);
        REQUIRE(o.state == OrderState::New);
        REQUIRE(o.version == 0);
    }
}

TEST_CASE("OSM: full lifecycle through Order object", "[state_machine]")
{
    Order o = make_order(OrderState::New);
    std::cout << "[STATE] Starting full lifecycle test\n";

    // New -> PendingNew
    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::Submit));
    REQUIRE(o.state == OrderState::PendingNew);
    REQUIRE(o.version == 1);

    // PendingNew -> Accepted
    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::Acknowledge));
    REQUIRE(o.state == OrderState::Accepted);
    REQUIRE(o.version == 2);

    // Accepted -> PartiallyFilled
    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::PartialFill));
    REQUIRE(o.state == OrderState::PartiallyFilled);
    REQUIRE(o.version == 3);

    // PartiallyFilled -> PartiallyFilled
    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::PartialFill));
    REQUIRE(o.state == OrderState::PartiallyFilled);
    REQUIRE(o.version == 4);

    // PartiallyFilled -> Filled
    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::Fill));
    REQUIRE(o.state == OrderState::Filled);
    REQUIRE(o.version == 5);
    REQUIRE(o.is_terminal());

    // Terminal -- no further transitions allowed.
    REQUIRE_FALSE(StateTransitionLogger::apply(o, OrderEvent::PartialFill));
    REQUIRE(o.version == 5); // unchanged
    std::cout << "[STATE] Full lifecycle complete: version=" << o.version << "\n";
}

TEST_CASE("OSM: cancel lifecycle through Order object", "[state_machine]")
{
    Order o = make_order(OrderState::New);
    std::cout << "[STATE] Starting cancel lifecycle test\n";

    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::Submit));
    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::Acknowledge));
    REQUIRE(o.state == OrderState::Accepted);

    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::RequestCancel));
    REQUIRE(o.state == OrderState::PendingCancel);

    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::CancelAck));
    REQUIRE(o.state == OrderState::Canceled);
    REQUIRE(o.is_terminal());
    std::cout << "[STATE] Cancel lifecycle complete\n";
}

TEST_CASE("OSM: replace lifecycle through Order object", "[state_machine]")
{
    Order o = make_order(OrderState::New);
    std::cout << "[STATE] Starting replace lifecycle test\n";

    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::Submit));
    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::Acknowledge));
    REQUIRE(o.state == OrderState::Accepted);

    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::RequestReplace));
    REQUIRE(o.state == OrderState::PendingReplace);

    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::ReplaceAck));
    REQUIRE(o.state == OrderState::Accepted);

    // Can still fill after replace.
    REQUIRE(StateTransitionLogger::apply(o, OrderEvent::Fill));
    REQUIRE(o.state == OrderState::Filled);
    std::cout << "[STATE] Replace lifecycle complete\n";
}

// ============================================================================
// valid_events utility
// ============================================================================

TEST_CASE("OSM: valid_events for New returns only Submit", "[state_machine]")
{
    auto events = OrderStateMachine::valid_events(OrderState::New);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0] == OrderEvent::Submit);
}

TEST_CASE("OSM: valid_events for Filled returns empty", "[state_machine]")
{
    auto events = OrderStateMachine::valid_events(OrderState::Filled);
    REQUIRE(events.empty());
}

TEST_CASE("OSM: valid_events for Accepted returns expected set", "[state_machine]")
{
    auto events = OrderStateMachine::valid_events(OrderState::Accepted);
    // Should include: PartialFill, Fill, RequestCancel, Reject, Expire, RequestReplace
    REQUIRE(events.size() == 6);
}

// ============================================================================
// String conversion
// ============================================================================

TEST_CASE("OSM: to_string for states", "[state_machine]")
{
    CHECK(std::string(OrderStateMachine::to_string(OrderState::New)) == "New");
    CHECK(std::string(OrderStateMachine::to_string(OrderState::PendingNew)) == "PendingNew");
    CHECK(std::string(OrderStateMachine::to_string(OrderState::Filled)) == "Filled");
    CHECK(std::string(OrderStateMachine::to_string(OrderState::Canceled)) == "Canceled");
}

TEST_CASE("OSM: to_string for events", "[state_machine]")
{
    CHECK(std::string(OrderStateMachine::to_string(OrderEvent::Submit)) == "Submit");
    CHECK(std::string(OrderStateMachine::to_string(OrderEvent::Fill)) == "Fill");
    CHECK(std::string(OrderStateMachine::to_string(OrderEvent::CancelAck)) == "CancelAck");
}
