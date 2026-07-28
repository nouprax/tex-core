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

# conformance: one CTest per case of the shared render-tree corpus. The
# manifest is the sole case list; CMake validates its shape and fails the
# configure on drift, so a case cannot silently drop out of the suite.
set(TEX_CORE_RENDER_TREE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../specs/render-tree")
set(TEX_CORE_RENDER_TREE_MANIFEST "${TEX_CORE_RENDER_TREE_DIR}/manifest.json")
if(NOT EXISTS "${TEX_CORE_RENDER_TREE_MANIFEST}")
    message(FATAL_ERROR "Shared render-tree manifest is missing: ${TEX_CORE_RENDER_TREE_MANIFEST}")
endif()
# Re-run the configure when the manifest changes; file(READ) alone does not
# register a configure dependency, and a stale test list would let a removed
# case keep passing.
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${TEX_CORE_RENDER_TREE_MANIFEST}")
file(READ "${TEX_CORE_RENDER_TREE_MANIFEST}" TEX_CORE_RENDER_TREE_JSON)
string(JSON TEX_CORE_RENDER_TREE_SCHEMA GET "${TEX_CORE_RENDER_TREE_JSON}" schemaVersion)
if(NOT TEX_CORE_RENDER_TREE_SCHEMA EQUAL 4)
    message(FATAL_ERROR "Unsupported render-tree manifest schema: ${TEX_CORE_RENDER_TREE_SCHEMA}")
endif()
string(JSON TEX_CORE_RENDER_TREE_CASE_COUNT LENGTH "${TEX_CORE_RENDER_TREE_JSON}" cases)
if(TEX_CORE_RENDER_TREE_CASE_COUNT LESS 1)
    message(FATAL_ERROR "Shared render-tree manifest contains no cases")
endif()

add_executable(tex-core-test-conformance conformance.c)
target_link_libraries(tex-core-test-conformance PRIVATE tex-core-test-support)

math(EXPR TEX_CORE_RENDER_TREE_CASE_LAST "${TEX_CORE_RENDER_TREE_CASE_COUNT} - 1")
foreach(case_index RANGE 0 ${TEX_CORE_RENDER_TREE_CASE_LAST})
    string(
        JSON
        case_name
        GET
        "${TEX_CORE_RENDER_TREE_JSON}"
        cases
        ${case_index}
        name)
    string(
        JSON
        case_input
        GET
        "${TEX_CORE_RENDER_TREE_JSON}"
        cases
        ${case_index}
        input)
    string(
        JSON
        case_expected
        GET
        "${TEX_CORE_RENDER_TREE_JSON}"
        cases
        ${case_index}
        expected)
    string(
        JSON
        case_mode
        GET
        "${TEX_CORE_RENDER_TREE_JSON}"
        cases
        ${case_index}
        compileOptions
        mode)
    string(
        JSON
        case_outcome
        GET
        "${TEX_CORE_RENDER_TREE_JSON}"
        cases
        ${case_index}
        outcome)
    if(NOT case_input STREQUAL "${case_name}.tex" OR NOT case_expected STREQUAL "${case_name}.tree")
        message(FATAL_ERROR "Render-tree case ${case_name} has non-canonical paths")
    endif()
    if(NOT case_mode MATCHES "^(document|mathInline|mathDisplay)$")
        message(FATAL_ERROR "Render-tree case ${case_name} has an unknown mode: ${case_mode}")
    endif()
    if(NOT case_outcome MATCHES "^(tree|error)$")
        message(FATAL_ERROR "Render-tree case ${case_name} has an unknown outcome: ${case_outcome}")
    endif()
    add_test(NAME conformance-${case_name}
             COMMAND tex-core-test-conformance "${TEX_CORE_RENDER_TREE_DIR}/${case_input}"
                     "${TEX_CORE_RENDER_TREE_DIR}/${case_expected}" "${case_mode}" "${case_outcome}")
    set_tests_properties(conformance-${case_name} PROPERTIES LABELS conformance)
endforeach()

if(NOT WIN32)
    find_package(Threads REQUIRED)
    tex_core_add_suite(threads)
    target_link_libraries(tex-core-test-threads PRIVATE Threads::Threads)

    tex_core_add_suite(cli $<TARGET_FILE:tex-core>)
endif()
