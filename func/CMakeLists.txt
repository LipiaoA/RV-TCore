
add_executable(rv_func
    main.cpp
    func_model.cpp
)

target_link_libraries(rv_func PRIVATE
    memory
    util
    tools
    isa
)
