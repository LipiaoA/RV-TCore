add_library(backend STATIC
    commit_unit.cpp
    execute_unit.cpp
    issue_unit.cpp
    regfile_unit.cpp
    vregfile_unit.cpp
    tregfile_unit.cpp
    rob.cpp
    bru.cpp
    alu.cpp
    stx.cpp
    fpu.cpp
    prf.cpp
    vprf.cpp
    tprf.cpp
    vector/vector_unit.cpp
    tile/tile_unit.cpp
    lsu/lsu.cpp
    lsu/store_buffer.cpp
    lsu/agu.cpp
)

target_include_directories(backend PUBLIC
    ${CMAKE_SOURCE_DIR}/include/issue
    ${CMAKE_SOURCE_DIR}/include/execute
    ${CMAKE_SOURCE_DIR}/include/execute/vector
    ${CMAKE_SOURCE_DIR}/include/execute/tile
    ${CMAKE_SOURCE_DIR}/include/execute/lsu
    ${CMAKE_SOURCE_DIR}/include/commit
    ${CMAKE_SOURCE_DIR}/include/rob
    ${CMAKE_SOURCE_DIR}/include/rename
    ${CMAKE_SOURCE_DIR}/include/decode
    ${CMAKE_SOURCE_DIR}/include/memory
    ${CMAKE_SOURCE_DIR}/include/bpred
)

target_link_libraries(backend PUBLIC
    util
    tools
    memory
    isa
    verify
)
