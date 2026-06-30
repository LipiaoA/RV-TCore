
add_library(isa STATIC
    decode.cpp
    inst_exec.cpp
)

target_include_directories(isa PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/isa
    ${CMAKE_SOURCE_DIR}/include/decode
    ${CMAKE_SOURCE_DIR}/include/memory
    ${CMAKE_SOURCE_DIR}/include/tools
)

target_link_libraries(isa PUBLIC util memory)
