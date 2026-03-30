add_library(sor_state STATIC
    order_state_machine.cpp
)

target_include_directories(sor_state
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_state
    PUBLIC
        sor_core
)
