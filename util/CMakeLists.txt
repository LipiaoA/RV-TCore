# 例如 hexdump 工具
add_library(util STATIC
    util.cpp
    arg_parser.cpp
    ../tools/log/log.cpp
)

target_include_directories(util  PUBLIC
    ${CMAKE_SOURCE_DIR}/include/log
    ${CMAKE_SOURCE_DIR}/include/util
)
