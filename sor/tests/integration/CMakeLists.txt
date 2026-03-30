# ── Integration test sources ──────────────────────────────────────────────────
set(INTEGRATION_TEST_SOURCES
    test_order_lifecycle.cpp
    test_venue_failover.cpp
    test_partial_fills.cpp
)

add_executable(sor_integration_tests ${INTEGRATION_TEST_SOURCES})

target_include_directories(sor_integration_tests
    PRIVATE ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_integration_tests
    PRIVATE
        Catch2::Catch2WithMain
        sor_core
        sor_state
        sor_risk
        sor_routing
        sor_market_data
        sor_connectors
        sor_gateway
        sor_execution
        sor_infra
        sor_utils
)

catch_discover_tests(sor_integration_tests)
