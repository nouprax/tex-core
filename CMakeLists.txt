cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TEX_CORE_VERSION LIMIT_COUNT 1)
if(NOT TEX_CORE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a stable semantic version")
endif()
project(
    tex-core
    VERSION "${TEX_CORE_VERSION}"
    LANGUAGES C)

enable_testing()
add_subdirectory(packages/tex-core)
