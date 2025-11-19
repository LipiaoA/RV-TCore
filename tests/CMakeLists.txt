# 测试 memory
add_executable(memory_test memory_test.cpp)
add_executable(branch_test branch_test.cpp)
add_executable(btb_test btb_test.cpp)
add_executable(cache_test cache_test.cpp)
add_executable(parser_test parser_test.cpp)
add_executable(log_test log_test.cpp)

target_link_libraries(memory_test PRIVATE memory util tools )
target_link_libraries(branch_test PRIVATE fetch)
target_link_libraries(btb_test PRIVATE    fetch)
target_link_libraries(cache_test PRIVATE  fetch memory)
target_link_libraries(parser_test PRIVATE util)
target_link_libraries(log_test PRIVATE tools)

