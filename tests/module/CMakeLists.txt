add_executable(commit_test commit_test.cpp)
add_executable(decode_test decode_test.cpp)
add_executable(execute_test execute_test.cpp)
add_executable(inst_exec_test inst_exec_test.cpp)
add_executable(issue_test issue_test.cpp)
add_executable(lsu_test lsu_test.cpp)
add_executable(perf_stats_test perf_stats_test.cpp)
add_executable(rename_test rename_test.cpp)
add_executable(vector_regfile_test vector_regfile_test.cpp)
add_executable(fetch_test fetch_test.cpp)

# fetch 在 rename_unit 中引用 IssueUnit::is_*，须先于 backend 链接，
# 否则 GNU ld 在处理完 backend 后才从 fetch 拉出 rename_unit.o，无法回头拉取 issue_unit.o。
set(_module_test_libs fetch backend memory tools util)

target_link_libraries(commit_test PRIVATE ${_module_test_libs})
target_link_libraries(decode_test PRIVATE ${_module_test_libs})
target_link_libraries(execute_test PRIVATE ${_module_test_libs})
target_link_libraries(inst_exec_test PRIVATE ${_module_test_libs})
target_link_libraries(issue_test PRIVATE ${_module_test_libs})
target_link_libraries(lsu_test PRIVATE ${_module_test_libs})
target_link_libraries(perf_stats_test PRIVATE ${_module_test_libs})
target_link_libraries(rename_test PRIVATE ${_module_test_libs})
target_link_libraries(vector_regfile_test PRIVATE ${_module_test_libs})
target_link_libraries(fetch_test PRIVATE ${_module_test_libs})

unset(_module_test_libs)
