
add_library(tools STATIC
    elf/elf_l.cpp
    log/log.cpp
)

target_include_directories(tools PUBLIC
    ${CMAKE_SOURCE_DIR}/include/tools
    ${CMAKE_SOURCE_DIR}/include/elf
    ${CMAKE_SOURCE_DIR}/include/log
    ${CMAKE_SOURCE_DIR}/include/util
    ${CMAKE_SOURCE_DIR}/include/memory
)
