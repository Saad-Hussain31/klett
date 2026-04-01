# ── Unit test sources ─────────────────────────────────────────────────────────
set(UNIT_TEST_SOURCES
    test_order_state_machine.cpp
    test_risk_manager.cpp
    test_routing_strategies.cpp
    test_market_data.cpp
    test_fixed_point.cpp
    test_memory_pool.cpp
    test_alpaca_normalizer.cpp
    test_feed_quality_monitor.cpp
    test_thread_safe_aggregator.cpp
    test_sor_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_main_listener.cpp
)

add_executable(sor_unit_tests ${UNIT_TEST_SOURCES})

target_include_directories(sor_unit_tests
    PRIVATE ${PROJECT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(sor_unit_tests
    PRIVATE
        Catch2::Catch2WithMain
        sor
)

catch_discover_tests(sor_unit_tests)
