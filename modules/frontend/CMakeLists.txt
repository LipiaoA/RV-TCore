add_library(fetch STATIC
    bpred/hybrid_predictor.cpp,
	branch_predictor.cpp,
	fetch_unit.cpp,
)

target_include_directories(fetch PUBLIC
    ${CMAKE_SOURCE_DIR}/include/bpred
    ${CMAKE_SOURCE_DIR}/include/fetch
)

target_link_libraries(fetch PUBLIC
    util
    tools
)
