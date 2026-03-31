include(FetchContent)

# ── nlohmann/json ────────────────────────────────────────────────────────────
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# ── spdlog ───────────────────────────────────────────────────────────────────
set(SPDLOG_FMT_EXTERNAL    OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE   OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH     OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL         OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
    GIT_SHALLOW    TRUE
)

# ── yaml-cpp ─────────────────────────────────────────────────────────────────
set(YAML_CPP_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS      OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB    OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL          OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    GIT_SHALLOW    TRUE
)

# ── libzmq (C library – required by cppzmq) ─────────────────────────────────
set(ZMQ_BUILD_TESTS          OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS              OFF CACHE BOOL "" FORCE)
set(WITH_DOCS                OFF CACHE BOOL "" FORCE)
set(WITH_PERF_TOOL           OFF CACHE BOOL "" FORCE)
set(ENABLE_DRAFTS            OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED             OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC             ON  CACHE BOOL "" FORCE)

FetchContent_Declare(
    libzmq
    GIT_REPOSITORY https://github.com/zeromq/libzmq.git
    GIT_TAG        v4.3.5
    GIT_SHALLOW    TRUE
)

# ── cppzmq (header-only C++ binding) ────────────────────────────────────────
set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    cppzmq
    GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
    GIT_TAG        v4.10.0
    GIT_SHALLOW    TRUE
)

# ── prometheus-cpp ───────────────────────────────────────────────────────────
set(ENABLE_PUSH               OFF CACHE BOOL "" FORCE)
set(ENABLE_PULL               ON  CACHE BOOL "" FORCE)
set(ENABLE_COMPRESSION        OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING            OFF CACHE BOOL "" FORCE)
set(GENERATE_PKGCONFIG        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    prometheus-cpp
    GIT_REPOSITORY https://github.com/jupp0r/prometheus-cpp.git
    GIT_TAG        v1.2.4
    GIT_SHALLOW    TRUE
)

# ── Catch2 (test-only) ──────────────────────────────────────────────────────
if(SOR_BUILD_TESTS)
    set(CATCH_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
    set(CATCH_BUILD_TESTING    OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_DOCS     OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS   OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
        GIT_SHALLOW    TRUE
    )
endif()

# ── Protobuf (system install) ───────────────────────────────────────────────
find_package(Protobuf QUIET)
if(Protobuf_FOUND)
    message(STATUS "Found Protobuf: ${Protobuf_VERSION}")
else()
    message(STATUS "Protobuf not found – proto targets will be unavailable")
endif()

# ── Fetch everything in one shot ─────────────────────────────────────────────
# libzmq must be available before cppzmq so that find_package(ZeroMQ) inside
# cppzmq resolves to the FetchContent-provided target.
FetchContent_MakeAvailable(nlohmann_json spdlog yaml-cpp libzmq)

# Point cppzmq's find_package(ZeroMQ) at the libzmq we just built.
set(ZeroMQ_DIR "${libzmq_BINARY_DIR}" CACHE PATH "" FORCE)
FetchContent_MakeAvailable(cppzmq)

FetchContent_MakeAvailable(prometheus-cpp)

if(SOR_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
endif()
