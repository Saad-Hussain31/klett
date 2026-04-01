cmake_minimum_required(VERSION 3.20)
project(luft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────
option(LUFT_BUILD_UI      "Build the UI (requires SDL2 + OpenGL)" OFF)
option(LUFT_BUILD_TESTS   "Build unit and integration tests"      ON)
option(LUFT_COVERAGE       "Enable gcov code coverage"            OFF)

# ── Coverage flags ───────────────────────────
if(LUFT_COVERAGE)
    message(STATUS "Coverage enabled — adding --coverage flags")
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage -O0 -g)
    add_link_options(--coverage -fprofile-arcs -ftest-coverage)
endif()

# ── Compiler warnings ───────────────────────
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# ═══════════════════════════════════════════════
# Core simulation library
# ═══════════════════════════════════════════════
set(CORE_SOURCES
    src/core/atmosphere.cpp
    src/core/wind_model.cpp
    src/core/aerodynamics.cpp
    src/core/engine_model.cpp
    src/core/flight_dynamics.cpp
    src/core/simulation_engine.cpp
    src/core/input_manager.cpp
    src/core/config.cpp
    src/core/logger.cpp
    src/core/net/socket.cpp
    src/core/net/message_codec.cpp
    src/core/net/network_service.cpp
)

add_library(luft_core STATIC ${CORE_SOURCES})
target_include_directories(luft_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_include_directories(luft_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core/net)
target_compile_features(luft_core PUBLIC cxx_std_20)

# Networking requires pthreads on Linux
find_package(Threads REQUIRED)
target_link_libraries(luft_core PUBLIC Threads::Threads)

# ═══════════════════════════════════════════════
# UI library (optional)
# ═══════════════════════════════════════════════
if(LUFT_BUILD_UI)
    # Fetch SDL2
    include(FetchContent)
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.10
        GIT_SHALLOW    TRUE
    )
    set(SDL_SHARED  OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC  ON  CACHE BOOL "" FORCE)
    set(SDL_TEST    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)

    # Fetch Dear ImGui
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.7
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)

    # Build ImGui as a static library
    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static)

    find_package(OpenGL REQUIRED)

    add_library(luft_ui STATIC src/ui/ui_app.cpp)
    target_include_directories(luft_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)
    target_link_libraries(luft_ui PUBLIC luft_core imgui_lib OpenGL::GL)
    target_compile_definitions(luft_ui PUBLIC LUFT_HAS_UI=1)
endif()

# ═══════════════════════════════════════════════
# Driver executable
# ═══════════════════════════════════════════════
add_executable(luft_sim src/app/main.cpp)
target_link_libraries(luft_sim PRIVATE luft_core)

if(LUFT_BUILD_UI)
    target_link_libraries(luft_sim PRIVATE luft_ui)
    target_compile_definitions(luft_sim PRIVATE LUFT_HAS_UI=1)
endif()

# ═══════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════
if(LUFT_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Collect test sources
    file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)

    add_executable(luft_tests ${TEST_SOURCES})
    target_link_libraries(luft_tests PRIVATE luft_core GTest::gtest_main)
    target_include_directories(luft_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
    target_include_directories(luft_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core/net)

    include(GoogleTest)
    gtest_discover_tests(luft_tests)
endif()
