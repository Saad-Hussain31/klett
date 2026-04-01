cmake_minimum_required(VERSION 3.20)
project(sor VERSION 1.0.0 LANGUAGES CXX)

# ── C++ Standard ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build options ────────────────────────────────────────────────────────────
option(SOR_BUILD_TESTS    "Build unit and integration tests"   ON)
option(SOR_BUILD_GUI      "Build Dear ImGui trading UI"        OFF)
option(SOR_BUILD_PYTHON   "Build Python bindings"              OFF)
option(SOR_USE_SANITIZERS "Enable ASan + UBSan"                OFF)
option(ENABLE_COVERAGE    "Enable coverage reporting"          OFF)

# ── Common compile warnings ──────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# ── Release optimisations ────────────────────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native -DNDEBUG)
endif()

# ── Sanitizers ───────────────────────────────────────────────────────────────
if(SOR_USE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# ── Coverage ─────────────────────────────────────────────────────────────────
if(ENABLE_COVERAGE)
    add_compile_options(-fprofile-arcs -ftest-coverage -O0 -g)
    add_link_options(-fprofile-arcs -ftest-coverage)
endif()

# ── Third-party dependencies ─────────────────────────────────────────────────
add_subdirectory(third_party)

# ── Consolidated library: libsor ─────────────────────────────────────────────
add_library(sor STATIC
    # core
    core/types.cpp
    core/order.cpp
    core/memory_pool.cpp
    core/fixed_point.cpp

    # state
    state/order_state_machine.cpp

    # risk
    risk/risk_manager.cpp
    risk/rate_limiter.cpp
    risk/kill_switch.cpp

    # market data
    market_data/aggregator.cpp
    market_data/book.cpp
    market_data/feed_handler.cpp
    market_data/replay_engine.cpp
    market_data/feed_quality_monitor.cpp
    market_data/provider_factory.cpp
    market_data/alpaca/alpaca_normalizer.cpp
    market_data/alpaca/alpaca_provider.cpp

    # routing
    routing/engine.cpp
    routing/best_price.cpp
    routing/liquidity_sweep.cpp
    routing/smart_ioc.cpp
    routing/vwap.cpp

    # connectors
    connectors/venue_adapter.cpp
    connectors/simulated_exchange.cpp
    connectors/fix_adapter.cpp

    # execution
    execution/execution_handler.cpp
    execution/fill_manager.cpp

    # gateway
    gateway/gateway.cpp
    gateway/fix_gateway.cpp
    gateway/api_gateway.cpp
    gateway/zmq_transport.cpp

    # infra
    infra/config.cpp
    infra/logging.cpp
    infra/metrics.cpp
    infra/tracing.cpp

    # utils
    utils/time_utils.cpp
    utils/string_utils.cpp

    # ui (headless-safe controller logic)
    ui/sor_controller.cpp
)

target_include_directories(sor
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        prometheus-cpp::pull
        cppzmq-static
)

if(SOR_USE_LIVE_FEED)
    target_link_libraries(sor PUBLIC ixwebsocket)
    target_compile_definitions(sor PUBLIC SOR_HAS_LIVE_FEED=1)
endif()

# ── Main executable: sor_app ─────────────────────────────────────────────────
add_executable(sor_app app/main.cpp)
target_link_libraries(sor_app PRIVATE sor)

# ── GUI executable: sor_gui ─────────────────────────────────────────────────
if(SOR_BUILD_GUI)
    find_package(OpenGL REQUIRED)

    # ImGui sources compiled directly into the executable
    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )

    add_executable(sor_gui
        ui/sor_ui_app.cpp
        ui/sor_ui.cpp
        ui/panels/order_entry.cpp
        ui/panels/order_book.cpp
        ui/panels/market_data.cpp
        ui/panels/execution.cpp
        ui/panels/log_panel.cpp
        ui/panels/controls.cpp
        ${IMGUI_SOURCES}
    )

    target_include_directories(sor_gui PRIVATE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )

    target_link_libraries(sor_gui PRIVATE
        sor
        SDL2::SDL2-static
        SDL2::SDL2main
        OpenGL::GL
    )
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
if(SOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Coverage targets ─────────────────────────────────────────────────────────
if(ENABLE_COVERAGE AND SOR_BUILD_TESTS)
    find_program(LCOV lcov)
    find_program(GENHTML genhtml)

    if(LCOV AND GENHTML)
        add_custom_target(coverage
            COMMENT "Running tests and collecting coverage..."
            COMMAND ${LCOV} --directory ${CMAKE_BINARY_DIR} --zerocounters
            COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure || true
            COMMAND ${LCOV} --directory ${CMAKE_BINARY_DIR} --capture --output-file ${CMAKE_BINARY_DIR}/coverage.info
            COMMAND ${LCOV} --remove ${CMAKE_BINARY_DIR}/coverage.info
                "*/third_party/*" "*/build*/_deps/*" "/usr/*" "*/tests/*" "*/test_*"
                --output-file ${CMAKE_BINARY_DIR}/coverage_filtered.info
            COMMAND ${LCOV} --summary ${CMAKE_BINARY_DIR}/coverage_filtered.info
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )

        add_custom_target(coverage_html
            COMMENT "Generating HTML coverage report..."
            DEPENDS coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
            COMMAND ${GENHTML} ${CMAKE_BINARY_DIR}/coverage_filtered.info
                --output-directory ${CMAKE_BINARY_DIR}/coverage
                --title "SOR Code Coverage"
                --legend --branch-coverage
            COMMAND ${CMAKE_COMMAND} -E echo "Coverage report: ${CMAKE_BINARY_DIR}/coverage/index.html"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    else()
        message(WARNING "lcov/genhtml not found -- coverage targets disabled")
    endif()
endif()

# ── Install targets ──────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS sor sor_app
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY
    core/ routing/ market_data/ connectors/ gateway/
    risk/ state/ execution/ infra/ utils/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sor
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
