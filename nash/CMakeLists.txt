cmake_minimum_required(VERSION 3.14)
project(nash VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")

# ── Library ──────────────────────────────────────────────────────────
add_library(nash_lib STATIC
    src/core/game.cpp
    src/core/strategy.cpp
    src/numerics/linear_algebra.cpp
    src/numerics/newton_solver.cpp
    src/numerics/path_follower.cpp
    src/algorithms/support_enumeration.cpp
    src/algorithms/lemke_howson.cpp
    src/algorithms/homotopy.cpp
)

target_include_directories(nash_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# ── Main executable ──────────────────────────────────────────────────
add_executable(nash_solve src/main.cpp)
target_link_libraries(nash_solve PRIVATE nash_lib)

# ── Tests ────────────────────────────────────────────────────────────
enable_testing()

add_executable(nash_tests
    tests/test_main.cpp
    tests/test_linear_algebra.cpp
    tests/test_game.cpp
    tests/test_support_enumeration.cpp
    tests/test_lemke_howson.cpp
    tests/test_homotopy.cpp
)

target_link_libraries(nash_tests PRIVATE nash_lib)

add_test(NAME NashTests COMMAND nash_tests)
