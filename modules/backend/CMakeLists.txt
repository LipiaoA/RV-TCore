add_library(backend STATIC
    commit_unit.cpp
    execute_unit.cpp
    issue_unit.cpp
    regfile_unit.cpp
    rob.cpp
    bru.cpp
    alu.cpp
    inst_exec.cpp
    prf.cpp
    lsu/lsu.cpp
    lsu/agu.cpp
)

target_include_directories(backend PUBLIC
    ${CMAKE_SOURCE_DIR}/include/issue
    ${CMAKE_SOURCE_DIR}/include/execute
    ${CMAKE_SOURCE_DIR}/include/execute/lsu
    ${CMAKE_SOURCE_DIR}/include/commit
    ${CMAKE_SOURCE_DIR}/include/rob
    ${CMAKE_SOURCE_DIR}/include/rename
    ${CMAKE_SOURCE_DIR}/include/decode
    ${CMAKE_SOURCE_DIR}/include/memory
)

target_link_libraries(backend PUBLIC
    util
    tools
    memory
)
