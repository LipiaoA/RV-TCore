add_library(core STATIC
    core.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_SOURCE_DIR}/include/core
)

target_link_libraries(core
    PUBLIC
    util
    tools
    memory
)
