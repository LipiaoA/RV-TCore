add_library(fetch STATIC
    fetch_unit.cpp
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
)

target_link_libraries(fetch PUBLIC
    util
    tools
    memory
)
