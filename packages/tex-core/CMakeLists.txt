# 3.21 matches the preset schema (CMakePresets.json version 3): the
# documented minimum tool must be able to use the recommended presets.
cmake_minimum_required(VERSION 3.21)

# tex-core always owns its version, even when a parent project adds this
# directory with add_subdirectory: deriving it from the parent's
# PROJECT_VERSION would misversion the library, the SONAME, and the
# pkg-config metadata against the committed public header.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" TEX_CORE_VERSION LIMIT_COUNT 1)
if(NOT TEX_CORE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a stable semantic version")
endif()
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/tex-core-version.h" TEX_CORE_HEADER_VERSION
     REGEX "#define TEX_CORE_VERSION_STRING")
if(NOT TEX_CORE_HEADER_VERSION MATCHES "\"${TEX_CORE_VERSION}\"")
    message(FATAL_ERROR "tex-core-version.h (${TEX_CORE_HEADER_VERSION}) drifted from VERSION (${TEX_CORE_VERSION})")
endif()
string(REGEX MATCH "^[0-9]+" TEX_CORE_VERSION_MAJOR "${TEX_CORE_VERSION}")
string(REGEX MATCH "^[0-9]+\\.[0-9]+" TEX_CORE_VERSION_MAJOR_MINOR "${TEX_CORE_VERSION}")

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    project(
        tex-core
        VERSION "${TEX_CORE_VERSION}"
        LANGUAGES C)
endif()

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "Do not build in-source. Configure through a preset: cmake --preset default")
endif()

option(TEX_CORE_TESTS "Build tex-core tests and enable testing" ON)
option(TEX_CORE_STATIC "Install the static libtex-core library" ON)
option(TEX_CORE_SHARED "Build and install the shared libtex-core library" ON)
option(TEX_CORE_LIB_FUZZER "Build the libFuzzer harness (explicit non-default target)" OFF)
option(TEX_CORE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(NOT TEX_CORE_STATIC AND NOT TEX_CORE_SHARED)
    # Installing with neither library would still export a package config
    # that includes a targets file no rule generates; fail the configure
    # instead of publishing a broken package.
    message(FATAL_ERROR "At least one of TEX_CORE_STATIC or TEX_CORE_SHARED must be ON")
endif()

# Default the build type only when tex-core itself is the project being
# built (this repository's root or this package standalone) and the
# generator is single-config: an embedding host project's cache and
# multi-config generators are never mutated.
if(PROJECT_NAME STREQUAL "tex-core" AND NOT CMAKE_BUILD_TYPE)
    get_property(TEX_CORE_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT TEX_CORE_MULTI_CONFIG)
        set(CMAKE_BUILD_TYPE
            "Release"
            CACHE STRING "Choose the type of build: Debug Release Asan Ubsan Tsan." FORCE)
    endif()
endif()

if(MSVC)
    # The allocation-injection seam uses C11 <stdatomic.h>; MSVC gates it
    # behind this flag even at /std:c11. _CRT_SECURE_NO_WARNINGS keeps the
    # portable standard-C calls (fopen) from tripping the MSVC-only C4996
    # deprecation under warnings-as-errors (markdown-core does the same).
    add_compile_options(/W4 /experimental:c11atomics)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()
if(TEX_CORE_WARNINGS_AS_ERRORS)
    if(MSVC)
        add_compile_options(/WX)
    else()
        add_compile_options(-Werror)
    endif()
endif()

# Sanitizer build types instrument the whole package: engine, CLI, and test
# runners. Undefined-behavior findings fail tests instead of merely printing.
if(CMAKE_BUILD_TYPE STREQUAL "Asan")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Ubsan")
    set(CMAKE_C_FLAGS
        "${CMAKE_C_FLAGS} -fsanitize=undefined -fno-sanitize-recover=undefined -g -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=undefined")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Tsan")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread -g -O1 -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=thread")
endif()

# The whole engine gets fuzzer coverage instrumentation; only the harness
# target links the libFuzzer runtime driver.
if(TEX_CORE_LIB_FUZZER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=fuzzer-no-link")
endif()

set(TEX_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

include(GNUInstallDirs)

add_subdirectory(core)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tex-core-config.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/tex-core-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tex-core")
# Pre-1.0 compatibility policy (docs/releasing.md): 0.x minors may break
# ABI, so package compatibility is SameMinorVersion until 1.0.0 promotes
# the contract to SameMajorVersion.
if(TEX_CORE_VERSION_MAJOR EQUAL 0)
    set(TEX_CORE_PACKAGE_COMPATIBILITY SameMinorVersion)
else()
    set(TEX_CORE_PACKAGE_COMPATIBILITY SameMajorVersion)
endif()
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tex-core-config-version.cmake"
    VERSION "${TEX_CORE_VERSION}"
    COMPATIBILITY ${TEX_CORE_PACKAGE_COMPATIBILITY})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tex-core-config.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/tex-core-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tex-core")

if(TEX_CORE_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(benchmarks)
endif()
if(TEX_CORE_LIB_FUZZER)
    add_subdirectory(fuzz)
endif()
