#pragma once

// Verbose test logging helpers for the SOR test suite.
// Provides a Catch2 event listener, logging macros, and domain-specific
// loggers for state transitions, routing decisions, and fills.

#include <catch2/catch_all.hpp>

#include "state/order_state_machine.h"
#include "core/types.h"
#include "core/order.h"

#include <iostream>
#include <string>

// ============================================================================
// Catch2 v3 Event Listener -- prints test case and section names
// ============================================================================

class VerboseTestListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseStarting(Catch::TestCaseInfo const& info) override {
        std::cout << "\n[TEST] ======== ENTER: " << info.name << " ========\n";
    }

    void testCaseEnded(Catch::TestCaseStats const& stats) override {
        const auto& info = stats.testInfo;
        const char* result = stats.totals.assertions.allOk() ? "PASSED" : "FAILED";
        std::cout << "[TEST] ======== EXIT:  " << info->name
                  << " [" << result << "] ========\n";
    }

    void sectionStarting(Catch::SectionInfo const& info) override {
        std::cout << "[TEST]   >> Section: " << info.name << "\n";
    }

    void sectionEnded(Catch::SectionStats const& stats) override {
        const auto& info = stats.sectionInfo;
        const char* result = stats.assertions.allOk() ? "PASSED" : "FAILED";
        std::cout << "[TEST]   << Section: " << info.name
                  << " [" << result << "]\n";
    }
};

// ============================================================================
// CHECK_WITH_LOG -- logs PASS/FAIL with expression text and a message
// ============================================================================

#define CHECK_WITH_LOG(expr, msg)                                              \
    do {                                                                        \
        bool _cwl_result = static_cast<bool>(expr);                            \
        std::cout << "[TEST] " << (_cwl_result ? "PASS" : "FAIL")             \
                  << ": (" << #expr << ") -- " << msg << "\n";                \
        CHECK(_cwl_result);                                                    \
    } while (false)

#define REQUIRE_WITH_LOG(expr, msg)                                            \
    do {                                                                        \
        bool _rwl_result = static_cast<bool>(expr);                            \
        std::cout << "[TEST] " << (_rwl_result ? "PASS" : "FAIL")             \
                  << ": (" << #expr << ") -- " << msg << "\n";                \
        REQUIRE(_rwl_result);                                                  \
    } while (false)

// ============================================================================
// StateTransitionLogger -- wraps OrderStateMachine::apply() with logging
// ============================================================================

class StateTransitionLogger {
public:
    /// Apply an event to a state and log the transition.
    static std::optional<sor::OrderState> apply(
        sor::OrderState from,
        sor::state::OrderEvent event)
    {
        auto result = sor::state::OrderStateMachine::apply(from, event);
        if (result.has_value()) {
            std::cout << "[STATE] Transition: "
                      << sor::state::OrderStateMachine::to_string(from)
                      << " --[" << sor::state::OrderStateMachine::to_string(event)
                      << "]--> " << sor::state::OrderStateMachine::to_string(*result)
                      << " (valid=true)\n";
        } else {
            std::cout << "[STATE] Transition: "
                      << sor::state::OrderStateMachine::to_string(from)
                      << " --[" << sor::state::OrderStateMachine::to_string(event)
                      << "]--> ??? (valid=false)\n";
        }
        return result;
    }

    /// Apply an event to an Order and log the transition.
    static bool apply(sor::Order& order, sor::state::OrderEvent event)
    {
        auto from = order.state;
        bool ok = sor::state::OrderStateMachine::apply(order, event);
        std::cout << "[STATE] Transition: "
                  << sor::state::OrderStateMachine::to_string(from)
                  << " --[" << sor::state::OrderStateMachine::to_string(event)
                  << "]--> " << sor::state::OrderStateMachine::to_string(order.state)
                  << " (valid=" << (ok ? "true" : "false")
                  << ", version=" << order.version << ")\n";
        return ok;
    }
};

// ============================================================================
// RoutingDecisionLogger -- prints routing decisions (slices)
// ============================================================================

namespace sor::routing {
struct RoutingDecision;
} // forward ref not needed since we use templates

class RoutingDecisionLogger {
public:
    /// Log all slices of a routing decision.
    template <typename Decision>
    static void log(const Decision& decision, const char* context = "")
    {
        if (context && context[0] != '\0') {
            std::cout << "[ROUTE] --- " << context << " ---\n";
        }
        std::cout << "[ROUTE] Decision valid=" << (decision.valid() ? "true" : "false")
                  << ", slices=" << decision.slices.size()
                  << ", total_qty=" << decision.total_quantity() << "\n";
        for (size_t i = 0; i < decision.slices.size(); ++i) {
            const auto& s = decision.slices[i];
            std::cout << "[ROUTE]   slice[" << i << "]: venue_id=" << s.venue_id
                      << " price=" << sor::to_double(s.price)
                      << " qty=" << s.quantity
                      << " type=" << static_cast<int>(s.type)
                      << " tif=" << static_cast<int>(s.tif) << "\n";
        }
    }
};

// ============================================================================
// FillLogger -- logs fill / execution report events
// ============================================================================

class FillLogger {
public:
    static void log_exec_report(const sor::ExecutionReport& rpt, const char* context = "")
    {
        if (context && context[0] != '\0') {
            std::cout << "[FILL] --- " << context << " ---\n";
        }
        std::cout << "[FILL] ExecReport: order_id=" << rpt.order_id
                  << " state=" << static_cast<int>(rpt.state)
                  << " last_px=" << sor::to_double(rpt.last_price)
                  << " last_qty=" << rpt.last_quantity
                  << " cum_qty=" << rpt.cum_quantity
                  << " leaves_qty=" << rpt.leaves_quantity
                  << " venue=" << rpt.venue_id << "\n";
    }

    static void log_order_state(const sor::Order& o, const char* context = "")
    {
        if (context && context[0] != '\0') {
            std::cout << "[FILL] --- " << context << " ---\n";
        }
        std::cout << "[FILL] Order id=" << o.id
                  << " state=" << static_cast<int>(o.state)
                  << " filled=" << o.filled_quantity
                  << " remaining=" << o.remaining_quantity
                  << " avg_px=" << sor::to_double(o.avg_fill_price) << "\n";
    }
};
