add_library(memory STATIC
    memory.cpp
)

target_include_directories(memory PUBLIC
    ${CMAKE_SOURCE_DIR}/include/memory
)

target_link_libraries(memory PUBLIC
    util
    tools
)
