add_library(sor_risk STATIC
    risk_manager.cpp
    rate_limiter.cpp
    kill_switch.cpp
)

target_include_directories(sor_risk
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_risk
    PUBLIC
        sor_core
)
