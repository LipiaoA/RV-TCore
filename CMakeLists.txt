cmake_minimum_required(VERSION 3.16)
project(RV-TCORE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项 & 输出目录
add_compile_options(-Wall -Wextra -Werror)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(ENABLE_TESTS "Build tests" ON)



# 头文件目录（全局）
include_directories(${CMAKE_SOURCE_DIR}/include)

# 子模块
add_subdirectory(modules/memory)
add_subdirectory(tools)
add_subdirectory(core)
add_subdirectory(util)
add_subdirectory(func)

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()

# 主程序（如果需要）
add_executable(rv-tcore main.cpp)
target_link_libraries(rv-tcore PRIVATE core tools util memory)
