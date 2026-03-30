add_library(sor_core STATIC
    types.cpp
    order.cpp
    memory_pool.cpp
    fixed_point.cpp
)

target_include_directories(sor_core
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_core
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)
