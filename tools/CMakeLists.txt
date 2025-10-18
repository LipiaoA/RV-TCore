add_library(tools STATIC
    queue.cpp
)

target_include_directories(tools PUBLIC
    ${CMAKE_SOURCE_DIR}/include/tools
)
