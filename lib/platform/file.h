#pragma once

/**
 * @file platform/file.h
 * @brief Cross-platform file I/O interface for ascii-chat
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides unified file operations with consistent behavior
 * across Windows and POSIX platforms.
 *
 * The interface provides:
 * - Platform-specific file mode constants
 * - Cross-platform file operations (declared in internal.h)
 *
 * @note File operations are declared in internal.h for internal use.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <sys/types.h>

// ============================================================================
// File Operations
// ============================================================================

// Safe file operations (declared in internal.h)

// File mode helpers
#ifdef _WIN32
#include <fcntl.h>
#define PLATFORM_O_RDONLY _O_RDONLY
#define PLATFORM_O_WRONLY _O_WRONLY
#define PLATFORM_O_RDWR _O_RDWR
#define PLATFORM_O_CREAT _O_CREAT
#define PLATFORM_O_EXCL _O_EXCL
#define PLATFORM_O_TRUNC _O_TRUNC
#define PLATFORM_O_APPEND _O_APPEND
#define PLATFORM_O_BINARY _O_BINARY
#else
#include <fcntl.h>
#define PLATFORM_O_RDONLY O_RDONLY
#define PLATFORM_O_WRONLY O_WRONLY
#define PLATFORM_O_RDWR O_RDWR
#define PLATFORM_O_CREAT O_CREAT
#define PLATFORM_O_EXCL O_EXCL
#define PLATFORM_O_TRUNC O_TRUNC
#define PLATFORM_O_APPEND O_APPEND
#define PLATFORM_O_BINARY 0 // Not needed on POSIX
#endif

// ============================================================================
// File Permission Validation
// ============================================================================

#include "../asciichat_errno.h"

/**
 * @brief Validate that a cryptographic key file has appropriate permissions
 *
 * Ensures that only the file owner can read the key file, preventing
 * unauthorized access to private cryptographic material.
 *
 * Platform-specific validation:
 *   - POSIX: Checks file mode permissions and verifies group/other bits are 0
 *   - Windows: Checks ACL (Access Control List) to ensure only owner has read access
 *
 * @param key_path Path to the key file to validate
 * @return ASCIICHAT_OK if permissions are appropriate, error code if too permissive
 *
 * @ingroup platform
 */
asciichat_error_t platform_validate_key_file_permissions(const char *key_path);

/** @} */