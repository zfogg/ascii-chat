#pragma once

/**
 * @file platform/windows_compat.h
 * @brief Wrapper for windows.h with C23 alignment compatibility
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides a single point to include windows.h with proper alignment.
 * The pragma pack ensures Windows SDK types get 8-byte alignment as expected,
 * then immediately restores default packing so application structs are unaffected.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#ifdef _WIN32
// Set 8-byte alignment for Windows SDK types (required for C23 compatibility)
#pragma pack(push, 8)

// Workaround for Windows SDK 10.0.26100.0 stralign.h bug
// The SDK's stralign.h uses _wcsicmp but doesn't declare it
// This is a known issue with newer Windows SDK versions when using C23
#include <stddef.h> // For wchar_t in C mode
#ifdef __cplusplus
extern "C" {
#endif
int __cdecl _wcsicmp(const wchar_t *, const wchar_t *);
#ifdef __cplusplus
}
#endif

#include <windows.h>
#pragma pack(pop)
#endif // _WIN32

/** @} */
