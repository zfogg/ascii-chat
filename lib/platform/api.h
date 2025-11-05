/**
 * @file platform/api.h
 * @ingroup platform
 * @brief DLL export/import macros for cross-platform symbol visibility
 *
 * This header defines the ASCIICHAT_API macro used to control symbol visibility
 * in shared libraries (DLLs on Windows, .so/.dylib on Unix).
 *
 * **Windows DLL Behavior:**
 * - When BUILDING_ASCIICHAT_DLL is defined: exports symbols from the DLL
 * - When using the DLL: imports symbols from the DLL
 * - Without proper export/import, each module gets its own copy of globals
 *
 * **Unix Behavior:**
 * - No special handling needed (symbols are visible by default)
 *
 * **Usage:**
 * ```c
 * // In header file (extern declaration):
 * extern ASCIICHAT_API int global_variable;
 * extern ASCIICHAT_API void my_function(void);
 *
 * // In source file (definition):
 * ASCIICHAT_API int global_variable = 42;
 * ASCIICHAT_API void my_function(void) { ... }
 * ```
 *
 * **CRITICAL: This header must have ZERO dependencies**
 * - Do not include any other headers
 * - Do not reference any types except built-in C types
 * - This allows it to be included first in common.h to avoid circular dependencies
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#ifdef _WIN32
#ifdef BUILDING_ASCIICHAT_DLL
/**
 * @def ASCIICHAT_API
 * @brief Export symbols when building the DLL
 *
 * When building the asciichat.dll, this expands to __declspec(dllexport)
 * which marks symbols for export in the DLL's export table.
 */
#define ASCIICHAT_API __declspec(dllexport)
#elif defined(BUILDING_STATIC_LIB)
/**
 * @def ASCIICHAT_API
 * @brief No special attributes for static library builds
 *
 * When building static libraries, symbols don't need import/export attributes.
 * They are linked directly into the final executable.
 */
#define ASCIICHAT_API
#else
/**
 * @def ASCIICHAT_API
 * @brief Import symbols when using the DLL
 *
 * When linking against asciichat.dll, this expands to __declspec(dllimport)
 * which tells the linker to import symbols from the DLL.
 *
 * @note This is only used when BUILDING_STATIC_LIB is not defined, meaning
 *       we're building code that will link against a DLL.
 */
#define ASCIICHAT_API __declspec(dllimport)
#endif
#else
/**
 * @def ASCIICHAT_API
 * @brief No-op on non-Windows platforms
 *
 * On Unix-like systems (Linux, macOS), symbols in shared libraries are
 * visible by default, so no special annotation is needed.
 */
#define ASCIICHAT_API
#endif
