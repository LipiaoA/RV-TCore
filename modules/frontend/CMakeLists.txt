add_library(fetch STATIC
    fetch_unit.cpp
    decode_unit.cpp
    rename_unit.cpp
    branch_predictor.cpp
    bpred/hybrid_predictor.cpp
    bpred/direct_btb.cpp
    bpred/indirect_btb.cpp
    bpred/local_predictor.cpp
    bpred/target_predictor.cpp
    bpred/ras_cache.cpp
)

target_include_directories(fetch PUBLIC
    ${CMAKE_SOURCE_DIR}/include/bpred
    ${CMAKE_SOURCE_DIR}/include/fetch
    ${CMAKE_SOURCE_DIR}/include/decode
    ${CMAKE_SOURCE_DIR}/include/dispatch
    ${CMAKE_SOURCE_DIR}/include/rename
    ${CMAKE_SOURCE_DIR}/include/isa
)

target_link_libraries(fetch PUBLIC
    util
    tools
    memory
)
