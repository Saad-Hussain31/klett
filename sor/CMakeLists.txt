cmake_minimum_required(VERSION 3.20)
project(sor VERSION 1.0.0 LANGUAGES CXX)

# ── C++ Standard ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build options ────────────────────────────────────────────────────────────
option(SOR_BUILD_TESTS    "Build unit and integration tests"   ON)
option(SOR_BUILD_PYTHON   "Build Python bindings"              OFF)
option(SOR_USE_SANITIZERS "Enable ASan + UBSan"                OFF)

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

# ── Main executable: sor_app ─────────────────────────────────────────────────
add_executable(sor_app app/main.cpp)
target_link_libraries(sor_app PRIVATE sor)

# ── Tests ────────────────────────────────────────────────────────────────────
if(SOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
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
