# Explicit non-default fuzz target: configure with -DTEX_CORE_LIB_FUZZER=ON
# (the `make libFuzzer` wrapper does this on the ASan preset) and build the
# `tex-core-fuzz` target by name.

add_executable(tex-core-fuzz EXCLUDE_FROM_ALL fuzz_compile.c)
target_link_libraries(tex-core-fuzz PRIVATE libtex-core_static)
target_compile_options(tex-core-fuzz PRIVATE -fsanitize=fuzzer)
target_link_options(tex-core-fuzz PRIVATE -fsanitize=fuzzer)
