
add_library(memory STATIC
    mem.cpp
    cache.cpp
    icache.cpp
    pseudo_lru.cpp
    tlb.cpp
)

target_include_directories(memory PUBLIC
    ${CMAKE_SOURCE_DIR}/include/memory
    ${CMAKE_SOURCE_DIR}/include/icache
)

target_link_libraries(memory PUBLIC
    util
    tools
)
