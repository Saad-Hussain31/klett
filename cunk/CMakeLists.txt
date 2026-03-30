cmake_minimum_required(VERSION 3.16)
project(concurrency_framework VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Library ───────────────────────────────────────────────
add_library(concurrency
    src/executor/ThreadPoolExecutor.cpp
    src/event/EventLoop.cpp
    src/scheduler/Scheduler.cpp
)

target_include_directories(concurrency
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(concurrency PUBLIC cxx_std_17)

# Threading support
find_package(Threads REQUIRED)
target_link_libraries(concurrency PUBLIC Threads::Threads)

# Compiler warnings
target_compile_options(concurrency PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ─── Tests ─────────────────────────────────────────────────
option(CONC_BUILD_TESTS "Build tests" ON)

if(CONC_BUILD_TESTS)
    enable_testing()

    # Helper function to add a test executable
    function(add_conc_test name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE concurrency)
        target_compile_options(${name} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
        )
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    add_conc_test(queue_test       tests/queue_test.cpp)
    add_conc_test(executor_test    tests/executor_test.cpp)
    add_conc_test(event_test       tests/event_test.cpp)
    add_conc_test(async_test       tests/async_test.cpp)
    add_conc_test(scheduler_test   tests/scheduler_test.cpp)
    add_conc_test(stress_test      tests/stress_test.cpp)
endif()

# ─── Examples ──────────────────────────────────────────────
option(CONC_BUILD_EXAMPLES "Build examples" ON)

if(CONC_BUILD_EXAMPLES)
    add_executable(example_basic examples/basic_usage.cpp)
    target_link_libraries(example_basic PRIVATE concurrency)
endif()

# ─── Install ───────────────────────────────────────────────
install(TARGETS concurrency
    EXPORT concurrencyTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT concurrencyTargets
    FILE concurrencyConfig.cmake
    NAMESPACE conc::
    DESTINATION lib/cmake/concurrency
)
