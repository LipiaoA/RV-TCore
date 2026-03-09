add_library(core STATIC
    core.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_SOURCE_DIR}/include/core
    ${CMAKE_SOURCE_DIR}/include/rob
    ${CMAKE_SOURCE_DIR}/include/commit
    ${CMAKE_SOURCE_DIR}/include/decode
    ${CMAKE_SOURCE_DIR}/include/dispatch
    ${CMAKE_SOURCE_DIR}/include/execute
    ${CMAKE_SOURCE_DIR}/include/issue
    ${CMAKE_SOURCE_DIR}/include/rename
)

target_link_libraries(core
    PUBLIC
    util
    tools
    memory
    fetch
    backend
)
