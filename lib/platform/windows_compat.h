#pragma once

/**
 * @file windows_compat.h
 * @brief Wrapper for windows.h with C23 alignment compatibility
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
#include <windows.h>
#pragma pack(pop)
#endif // _WIN32
