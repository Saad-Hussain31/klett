add_library(sor_infra STATIC
    config.cpp
    logging.cpp
    metrics.cpp
    tracing.cpp
)

target_include_directories(sor_infra
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_infra
    PUBLIC
        sor_core
        spdlog::spdlog
        yaml-cpp::yaml-cpp
        prometheus-cpp::pull
)
