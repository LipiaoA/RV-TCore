cmake_minimum_required(VERSION 3.16)
project(RV-TCORE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 编译选项 & 输出目录
# add_compile_options(-Wall -Wextra -Werror)
add_compile_options(-Wall -O0)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(ENABLE_TESTS "Build tests" ON)



# 头文件目录（全局）
include_directories(${CMAKE_SOURCE_DIR}/include)

# 子模块
add_subdirectory(modules/memory)
add_subdirectory(modules/frontend)
add_subdirectory(func)
add_subdirectory(tools)
add_subdirectory(core)
add_subdirectory(util)


if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()

add_executable(tcore main.cpp model.cpp)
target_link_libraries(tcore PRIVATE core tools util memory)
