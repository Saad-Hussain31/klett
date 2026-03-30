add_library(sor_execution STATIC
    execution_handler.cpp
    fill_manager.cpp
)

target_include_directories(sor_execution
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_execution
    PUBLIC
        sor_core
        sor_state
)
