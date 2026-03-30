add_library(sor_connectors STATIC
    venue_adapter.cpp
    simulated_exchange.cpp
    fix_adapter.cpp
)

target_include_directories(sor_connectors
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_connectors
    PUBLIC
        sor_core
        sor_state
        cppzmq-static
)
