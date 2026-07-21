cmake_minimum_required(VERSION 3.22.1)
project(tex_core_kotlin_android LANGUAGES C)

set(TEX_CORE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../.."
    CACHE PATH "Path to the tex-core repository root")
get_filename_component(TEX_CORE_ROOT "${TEX_CORE_ROOT}" ABSOLUTE)
set(TEX_CORE_CORE_DIR "${TEX_CORE_ROOT}/packages/tex-core/core")

add_library(
    tex_core_kotlin SHARED
    "${TEX_CORE_CORE_DIR}/dump.c"
    "${TEX_CORE_CORE_DIR}/error.c"
    "${TEX_CORE_CORE_DIR}/layout.c"
    "${TEX_CORE_CORE_DIR}/memory.c"
    "${TEX_CORE_CORE_DIR}/metrics.c"
    "${TEX_CORE_CORE_DIR}/parse.c"
    "${TEX_CORE_CORE_DIR}/scaled.c"
    "${TEX_CORE_CORE_DIR}/tex_core.c"
    "${TEX_CORE_CORE_DIR}/token.c"
    "${TEX_CORE_CORE_DIR}/utf8.c"
    "${TEX_CORE_ROOT}/packages/kotlin-tex-core/src/native/tex_core_kotlin_bridge.c"
    "${TEX_CORE_ROOT}/packages/kotlin-tex-core/src/native/tex_core_kotlin_jni.c")

target_include_directories(tex_core_kotlin PRIVATE "${TEX_CORE_CORE_DIR}" "${TEX_CORE_ROOT}/packages/tex-core/include"
                                                   "${TEX_CORE_ROOT}/packages/kotlin-tex-core/src/native")
target_compile_definitions(tex_core_kotlin PRIVATE TEX_CORE_STATIC_DEFINE)
target_link_options(tex_core_kotlin PRIVATE
                    "-Wl,--version-script=${TEX_CORE_ROOT}/packages/kotlin-tex-core/src/native/tex_core_kotlin.map")
target_compile_options(
    tex_core_kotlin
    PRIVATE $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:GNU>>:-Wall>
            $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:GNU>>:-Wextra>
            $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:GNU>>:-pedantic>)
set_target_properties(
    tex_core_kotlin
    PROPERTIES C_STANDARD 11
               C_STANDARD_REQUIRED YES
               C_EXTENSIONS NO
               C_VISIBILITY_PRESET hidden
               POSITION_INDEPENDENT_CODE ON
               OUTPUT_NAME "tex_core_kotlin")
