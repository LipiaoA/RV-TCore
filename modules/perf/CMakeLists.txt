add_library(perf STATIC
    perf.cpp
    topdown.cpp
)

target_include_directories(perf PUBLIC
    ${CMAKE_SOURCE_DIR}/include/perf
)

target_link_libraries(perf PUBLIC
    util
    tools
)
