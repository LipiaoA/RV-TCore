add_library(backend STATIC
    commit_unit.cpp
    execute_unit.cpp
    issue_unit.cpp
)

target_include_directories(backend PUBLIC
    ${CMAKE_SOURCE_DIR}/include/issue
    ${CMAKE_SOURCE_DIR}/include/execute
    ${CMAKE_SOURCE_DIR}/include/commit
)

target_link_libraries(backend PUBLIC
    util
    tools
    memory
)
