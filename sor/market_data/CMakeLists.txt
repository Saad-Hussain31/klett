add_library(sor_market_data STATIC
    aggregator.cpp
    book.cpp
    feed_handler.cpp
    replay_engine.cpp
)

target_include_directories(sor_market_data
    PUBLIC ${PROJECT_SOURCE_DIR}
)

target_link_libraries(sor_market_data
    PUBLIC
        sor_core
)
