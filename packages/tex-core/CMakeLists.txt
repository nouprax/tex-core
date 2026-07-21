cmake_minimum_required(VERSION 3.20)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" TEX_CORE_VERSION LIMIT_COUNT 1)
    if(NOT TEX_CORE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "VERSION must contain a stable semantic version")
    endif()
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

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
        "Release"
        CACHE STRING "Choose the type of build: Debug Release Asan Ubsan Tsan." FORCE)
endif()

if(MSVC)
    # The allocation-injection seam uses C11 <stdatomic.h>; MSVC gates it
    # behind this flag even at /std:c11.
    add_compile_options(/W4 /experimental:c11atomics)
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
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tex-core-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
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
