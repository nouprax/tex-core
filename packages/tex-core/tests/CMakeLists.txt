# CTest is the single source of truth for the C test graph. Suites are
# grouped by label:
#
#   api engine errors determinism allocfail threads cli conformance benchmark
#
# The default correctness preset excludes conformance and benchmark; the
# sanitizer correctness presets also exclude wall-clock complexity gates.
# Every suite is native C; the test tree has no scripting-language
# dependency and never touches the network.

add_library(tex-core-test-support STATIC support/harness.c)
target_include_directories(tex-core-test-support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(tex-core-test-support PUBLIC libtex-core_static)

function(tex_core_add_suite suite)
    add_executable(tex-core-test-${suite} ${suite}.c)
    target_link_libraries(tex-core-test-${suite} PRIVATE tex-core-test-support)
    # Suites may exercise internal seams (allocation injection) alongside
    # the public API.
    target_include_directories(tex-core-test-${suite} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core")
    add_test(NAME ${suite} COMMAND tex-core-test-${suite} ${ARGN})
    set_tests_properties(${suite} PROPERTIES LABELS ${suite})
endfunction()

tex_core_add_suite(api)
tex_core_add_suite(engine)
tex_core_add_suite(errors)
tex_core_add_suite(determinism)
tex_core_add_suite(allocfail)
tex_core_add_suite(conformance)

if(NOT WIN32)
    find_package(Threads REQUIRED)
    tex_core_add_suite(threads)
    target_link_libraries(tex-core-test-threads PRIVATE Threads::Threads)

    tex_core_add_suite(cli $<TARGET_FILE:tex-core>)
endif()
