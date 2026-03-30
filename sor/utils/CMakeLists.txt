add_library(sor_utils STATIC
    time_utils.cpp
    string_utils.cpp
)

target_include_directories(sor_utils
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_utils
    PUBLIC
        sor_core
)
