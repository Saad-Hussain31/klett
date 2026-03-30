add_library(sor_gateway STATIC
    gateway.cpp
    fix_gateway.cpp
    api_gateway.cpp
)

target_include_directories(sor_gateway
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_gateway
    PUBLIC
        sor_core
        sor_routing
        sor_risk
        sor_execution
        sor_state
        sor_market_data
        sor_connectors
        nlohmann_json::nlohmann_json
)
