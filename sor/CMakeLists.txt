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

# ── Library targets ──────────────────────────────────────────────────────────
add_subdirectory(core)
add_subdirectory(routing)
add_subdirectory(market_data)
add_subdirectory(connectors)
add_subdirectory(gateway)
add_subdirectory(risk)
add_subdirectory(state)
add_subdirectory(execution)
add_subdirectory(infra)
add_subdirectory(utils)

# ── Tests ────────────────────────────────────────────────────────────────────
if(SOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install targets ──────────────────────────────────────────────────────────
include(GNUInstallDirs)

set(SOR_INSTALLABLE_LIBS
    sor_core
    sor_routing
    sor_market_data
    sor_connectors
    sor_gateway
    sor_risk
    sor_state
    sor_execution
    sor_infra
    sor_utils
)

install(TARGETS ${SOR_INSTALLABLE_LIBS}
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
