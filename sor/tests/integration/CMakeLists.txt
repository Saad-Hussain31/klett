# ── Integration test sources ──────────────────────────────────────────────────
set(INTEGRATION_TEST_SOURCES
    test_order_lifecycle.cpp
    test_venue_failover.cpp
    test_partial_fills.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../test_main_listener.cpp
)

add_executable(sor_integration_tests ${INTEGRATION_TEST_SOURCES})

target_include_directories(sor_integration_tests
    PRIVATE ${PROJECT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(sor_integration_tests
    PRIVATE
        Catch2::Catch2WithMain
        sor
)

catch_discover_tests(sor_integration_tests)
