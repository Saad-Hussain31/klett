# ── Unit test sources ─────────────────────────────────────────────────────────
set(UNIT_TEST_SOURCES
    test_order_state_machine.cpp
    test_risk_manager.cpp
    test_routing_strategies.cpp
    test_market_data.cpp
    test_fixed_point.cpp
    test_memory_pool.cpp
)

add_executable(sor_unit_tests ${UNIT_TEST_SOURCES})

target_include_directories(sor_unit_tests
    PRIVATE ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_unit_tests
    PRIVATE
        Catch2::Catch2WithMain
        sor_core
        sor_state
        sor_risk
        sor_routing
        sor_market_data
)

catch_discover_tests(sor_unit_tests)
