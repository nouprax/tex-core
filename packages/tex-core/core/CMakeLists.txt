include(GenerateExportHeader)

set(TEX_CORE_HEADERS
    dump.h
    error.h
    layout.h
    memory.h
    metrics.h
    parse.h
    scaled.h
    token.h
    tree.h
    utf8.h
    "${TEX_CORE_INCLUDE_DIR}/tex_core.h")
set(TEX_CORE_SOURCES
    dump.c
    error.c
    layout.c
    memory.c
    metrics.c
    parse.c
    scaled.c
    tex_core.c
    token.c
    utf8.c
    ${TEX_CORE_HEADERS})

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tex-core-version.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/include/tex-core-version.h")

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

# The internal static archive always exists: the CLI and the test runners
# link private engine symbols through it, so the installed shared facade can
# export only the reviewed public API.
add_library(libtex-core_static STATIC ${TEX_CORE_SOURCES})
target_compile_definitions(libtex-core_static PUBLIC TEX_CORE_STATIC_DEFINE)
set_target_properties(libtex-core_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    set_target_properties(libtex-core_static PROPERTIES OUTPUT_NAME "tex-core_static" VERSION ${PROJECT_VERSION})
else()
    set_target_properties(libtex-core_static PROPERTIES OUTPUT_NAME "tex-core" VERSION ${PROJECT_VERSION})
endif()

set(TEX_CORE_TARGETS libtex-core_static)

if(TEX_CORE_SHARED)
    add_library(libtex-core SHARED ${TEX_CORE_SOURCES})
    set_target_properties(
        libtex-core
        PROPERTIES OUTPUT_NAME "tex-core"
                   VERSION ${PROJECT_VERSION}
                   SOVERSION ${PROJECT_VERSION_MAJOR})
    list(APPEND TEX_CORE_TARGETS libtex-core)
endif()

# The export header must come from the shared target: generating it from a
# static target makes CMake emit a no-op TEX_CORE_API, which under hidden
# visibility would strip every public symbol from the shared library.
# Static-only builds (the sanitizer presets) keep the no-op macros, which is
# correct for archives.
if(TEX_CORE_SHARED)
    set(TEX_CORE_EXPORT_SOURCE_TARGET libtex-core)
else()
    set(TEX_CORE_EXPORT_SOURCE_TARGET libtex-core_static)
endif()
generate_export_header(
    ${TEX_CORE_EXPORT_SOURCE_TARGET}
    BASE_NAME
    TEX_CORE
    EXPORT_MACRO_NAME
    TEX_CORE_API
    EXPORT_FILE_NAME
    "${CMAKE_CURRENT_BINARY_DIR}/include/tex-core-export.h")

foreach(target IN LISTS TEX_CORE_TARGETS)
    set_target_properties(
        ${target}
        PROPERTIES C_STANDARD 11
                   C_STANDARD_REQUIRED ON
                   C_EXTENSIONS OFF)
    target_include_directories(
        ${target}
        PUBLIC "$<BUILD_INTERFACE:${TEX_CORE_INCLUDE_DIR}>" "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
               "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()

add_executable(tex-core main.c)
set_target_properties(
    tex-core
    PROPERTIES C_STANDARD 11
               C_STANDARD_REQUIRED ON
               C_EXTENSIONS OFF)
target_link_libraries(tex-core PRIVATE libtex-core_static)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tex-core.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/tex-core.pc" @ONLY)

install(FILES "${TEX_CORE_INCLUDE_DIR}/tex_core.h" "${CMAKE_CURRENT_BINARY_DIR}/include/tex-core-export.h"
              "${CMAKE_CURRENT_BINARY_DIR}/include/tex-core-version.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tex-core.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
install(TARGETS tex-core RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Exactly one target is exported as tex-core::tex-core — the shared library
# when it exists, otherwise the public static archive. Exporting both would
# leak the internal libtex-core/libtex-core_static target names into every
# consumer; with both libraries enabled the static archive still installs,
# just without an export entry (consumers reach it through pkg-config
# --static).
if(TEX_CORE_SHARED)
    set(TEX_CORE_EXPORT_TARGET libtex-core)
elseif(TEX_CORE_STATIC)
    set(TEX_CORE_EXPORT_TARGET libtex-core_static)
else()
    set(TEX_CORE_EXPORT_TARGET "")
endif()
if(TEX_CORE_EXPORT_TARGET)
    set_target_properties(${TEX_CORE_EXPORT_TARGET} PROPERTIES EXPORT_NAME tex-core)
    install(
        TARGETS ${TEX_CORE_EXPORT_TARGET}
        EXPORT tex-core-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(
        EXPORT tex-core-targets
        NAMESPACE tex-core::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tex-core")
endif()
if(TEX_CORE_STATIC AND TEX_CORE_SHARED)
    install(TARGETS libtex-core_static ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
