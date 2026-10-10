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
// Include wchar.h first to get the proper declaration with dllimport
#include <wchar.h>

#include <windows.h>
// Capture SDK values before undefining names used by the application error enum.
// Enum constants preserve the expanded values; macro aliases would expand only
// when used, after the original macros have been undefined.
enum {
  WIN32_ERROR_BUFFER_OVERFLOW = ERROR_BUFFER_OVERFLOW,
  WIN32_ERROR_INVALID_STATE = ERROR_INVALID_STATE,
  WIN32_ERROR_FILE_NOT_FOUND = ERROR_FILE_NOT_FOUND,
  WIN32_ERROR_NOT_SUPPORTED = ERROR_NOT_SUPPORTED,
  WIN32_ERROR_INVALID_PASSWORD = ERROR_INVALID_PASSWORD,
  WIN32_ERROR_NOT_FOUND = ERROR_NOT_FOUND,
};
#undef ERROR_BUFFER_OVERFLOW
#undef ERROR_INVALID_STATE
#undef ERROR_FILE_NOT_FOUND
#undef ERROR_NOT_SUPPORTED
#undef ERROR_INVALID_PASSWORD
#undef ERROR_NOT_FOUND
#pragma pack(pop)
#endif // _WIN32

/** @} */
