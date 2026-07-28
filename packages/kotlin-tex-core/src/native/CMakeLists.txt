# Kotlin bridge targets, enabled from the root CMake graph by the Gradle
# build. The JNI shared library serves the JVM desktop payload; the static
# archive serves the Kotlin/Native cinterop. Both compile the bridge over
# the public facade only and export exactly the reviewed JNI symbol.

set(TEX_CORE_TXC1_SOURCE ${CMAKE_SOURCE_DIR}/packages/tex-core/bridge/tex_core_txc1.c)
set(TEX_CORE_TXC1_INCLUDE ${CMAKE_SOURCE_DIR}/packages/tex-core/bridge)

if(TEX_CORE_KOTLIN_JNI)
    find_package(JNI REQUIRED)

    add_library(tex_core_kotlin_jni SHARED tex_core_kotlin_bridge.c tex_core_kotlin_jni.c ${TEX_CORE_TXC1_SOURCE})
    target_include_directories(
        tex_core_kotlin_jni PRIVATE ${JNI_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR} ${TEX_CORE_TXC1_INCLUDE}
                                    ${CMAKE_SOURCE_DIR}/packages/tex-core/include)
    target_compile_definitions(tex_core_kotlin_jni PRIVATE TEX_CORE_STATIC_DEFINE)
    target_link_libraries(tex_core_kotlin_jni PRIVATE libtex-core_static)
    set_target_properties(
        tex_core_kotlin_jni
        PROPERTIES OUTPUT_NAME tex_core_kotlin
                   C_STANDARD 11
                   C_STANDARD_REQUIRED YES
                   C_EXTENSIONS NO
                   C_VISIBILITY_PRESET hidden
                   POSITION_INDEPENDENT_CODE ON)
    if(APPLE)
        target_link_options(tex_core_kotlin_jni PRIVATE
                            "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/tex_core_kotlin.exports")
    elseif(WIN32)
        target_sources(tex_core_kotlin_jni PRIVATE tex_core_kotlin.def)
    else()
        target_link_options(tex_core_kotlin_jni PRIVATE
                            "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/tex_core_kotlin.map")
    endif()
endif()

if(TEX_CORE_KOTLIN_NATIVE)
    add_library(tex_core_kotlin_native STATIC tex_core_kotlin_bridge.c ${TEX_CORE_TXC1_SOURCE})
    target_include_directories(tex_core_kotlin_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${TEX_CORE_TXC1_INCLUDE}
                                                              ${CMAKE_SOURCE_DIR}/packages/tex-core/include)
    target_compile_definitions(tex_core_kotlin_native PRIVATE TEX_CORE_STATIC_DEFINE)
    add_dependencies(tex_core_kotlin_native libtex-core_static)
    set_target_properties(
        tex_core_kotlin_native
        PROPERTIES OUTPUT_NAME tex_core_kotlin
                   C_STANDARD 11
                   C_STANDARD_REQUIRED YES
                   C_EXTENSIONS NO
                   POSITION_INDEPENDENT_CODE ON)
endif()
