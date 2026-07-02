add_library(verify STATIC
    ref_inst_record.cpp
    ref_inst_log.cpp
    ref_model_checker.cpp
)

target_include_directories(verify PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/verify
    ${CMAKE_SOURCE_DIR}/include/func
)

target_link_libraries(verify PUBLIC
    func
    isa
    tools
    util
)
