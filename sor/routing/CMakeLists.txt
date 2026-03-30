add_library(sor_routing STATIC
    engine.cpp
    best_price.cpp
    liquidity_sweep.cpp
    smart_ioc.cpp
    vwap.cpp
)

target_include_directories(sor_routing
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_routing
    PUBLIC
        sor_core
        sor_market_data
        sor_risk
)
