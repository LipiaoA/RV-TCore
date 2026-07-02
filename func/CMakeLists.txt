add_library(func STATIC
    func_model.cpp
    func_step_log.cpp
    ref_inst_record.cpp
)

target_include_directories(func PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/func
    ${CMAKE_SOURCE_DIR}/include/verify
)

target_link_libraries(func PUBLIC
    memory
    util
    isa
)

add_executable(rv_func
    main.cpp
)

target_link_libraries(rv_func PRIVATE
    func
    tools
)
