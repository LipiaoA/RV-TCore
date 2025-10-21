add_library(tools STATIC
    queue.cpp,
    elf/elf_l.cpp,
)

target_include_directories(tools PUBLIC
    ${CMAKE_SOURCE_DIR}/include/tools
    ${CMAKE_SOURCE_DIR}/include/elf
)
