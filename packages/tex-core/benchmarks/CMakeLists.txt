add_executable(tex-core-bench bench.c)
target_link_libraries(tex-core-bench PRIVATE libtex-core_static)

add_test(NAME benchmark-compile COMMAND tex-core-bench)
set_tests_properties(benchmark-compile PROPERTIES LABELS benchmark)
