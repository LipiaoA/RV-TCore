add_library(common STATIC
    queue.cpp
)

target_include_directories(common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
