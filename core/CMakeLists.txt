add_library(core STATIC
    core.cpp
    exec.cpp
    decoder.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(core
    PUBLIC
    common
    memory
    # 视情况是否依赖 tools 里的某些功能
)
