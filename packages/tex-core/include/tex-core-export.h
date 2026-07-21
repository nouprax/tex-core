/* Export macro for the public facade. Committed (not generated) so every
 * build system — CMake, SwiftPM, and the bindings to come — compiles the
 * same header. Shared builds define TEX_CORE_EXPORTS while compiling the
 * library; static builds define TEX_CORE_STATIC_DEFINE; consumers of the
 * shared library define neither. */

#ifndef TEX_CORE_EXPORT_H
#define TEX_CORE_EXPORT_H

#ifdef TEX_CORE_STATIC_DEFINE
#define TEX_CORE_API
#else
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef TEX_CORE_EXPORTS
#define TEX_CORE_API __declspec(dllexport)
#else
#define TEX_CORE_API __declspec(dllimport)
#endif
#else
#define TEX_CORE_API __attribute__((visibility("default")))
#endif
#endif

#endif
