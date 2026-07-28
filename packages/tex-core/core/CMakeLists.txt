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
    "${TEX_CORE_INCLUDE_DIR}/tex_core.h"
    "${TEX_CORE_INCLUDE_DIR}/tex-core-export.h"
    "${TEX_CORE_INCLUDE_DIR}/tex-core-version.h")
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

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

# The internal static archive always exists: the CLI and the test runners
# link private engine symbols through it, so the installed shared facade can
# export only the reviewed public API.
add_library(libtex-core_static STATIC ${TEX_CORE_SOURCES})
target_compile_definitions(libtex-core_static PUBLIC TEX_CORE_STATIC_DEFINE)
set_target_properties(libtex-core_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    set_target_properties(libtex-core_static PROPERTIES OUTPUT_NAME "tex-core_static" VERSION ${TEX_CORE_VERSION})
else()
    set_target_properties(libtex-core_static PROPERTIES OUTPUT_NAME "tex-core" VERSION ${TEX_CORE_VERSION})
endif()

set(TEX_CORE_TARGETS libtex-core_static)

# The export and version headers are committed files in the public include
# directory (SwiftPM and the other bindings compile the same headers with no
# generation step). The shared library alone defines TEX_CORE_EXPORTS, which
# under hidden visibility marks exactly the reviewed public API as visible;
# archives use the no-op macro through TEX_CORE_STATIC_DEFINE.
if(TEX_CORE_SHARED)
    add_library(libtex-core SHARED ${TEX_CORE_SOURCES})
    # Pre-1.0 SONAME policy (docs/releasing.md): every 0.x minor may break
    # ABI, so the SONAME carries major.minor until 1.0.0 collapses it to
    # the major alone. Advertising all of 0.x as one ABI would let an
    # incompatible 0.(x+1) replace 0.x at load time.
    if(TEX_CORE_VERSION_MAJOR EQUAL 0)
        set(TEX_CORE_SOVERSION ${TEX_CORE_VERSION_MAJOR_MINOR})
    else()
        set(TEX_CORE_SOVERSION ${TEX_CORE_VERSION_MAJOR})
    endif()
    set_target_properties(
        libtex-core
        PROPERTIES OUTPUT_NAME "tex-core"
                   VERSION ${TEX_CORE_VERSION}
                   SOVERSION ${TEX_CORE_SOVERSION}
                   DEFINE_SYMBOL TEX_CORE_EXPORTS)
    list(APPEND TEX_CORE_TARGETS libtex-core)
endif()

foreach(target IN LISTS TEX_CORE_TARGETS)
    set_target_properties(
        ${target}
        PROPERTIES C_STANDARD 11
                   C_STANDARD_REQUIRED ON
                   C_EXTENSIONS OFF)
    target_include_directories(
        ${target}
        PUBLIC "$<BUILD_INTERFACE:${TEX_CORE_INCLUDE_DIR}>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
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

install(FILES "${TEX_CORE_INCLUDE_DIR}/tex_core.h" "${TEX_CORE_INCLUDE_DIR}/tex-core-export.h"
              "${TEX_CORE_INCLUDE_DIR}/tex-core-version.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tex-core.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
if(TEX_CORE_STATIC)
    # The static consumer contract carries TEX_CORE_STATIC_DEFINE, matching
    # what the CMake package propagates from the static target.
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tex-core-static.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/tex-core-static.pc"
                   @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tex-core-static.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()
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
