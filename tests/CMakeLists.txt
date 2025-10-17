# 测试 common
add_executable(test_common test_common.cpp)
target_link_libraries(test_common PRIVATE common)

# 测试 memory
add_executable(test_memory test_memory.cpp)
target_link_libraries(test_memory PRIVATE memory common)

# 测试 core
add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE core common memory)
