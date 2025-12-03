#pragma once

/**
 * @file platform/windows_errno.h
 * @brief Windows errno compatibility definitions
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides POSIX-style errno constant definitions for Windows
 * compatibility. Windows headers do not define these constants by default,
 * so this header fills the gap to enable cross-platform code.
 *
 * CORE FEATURES:
 * ==============
 * - POSIX errno constants (EINVAL, ERANGE, ETIMEDOUT, etc.)
 * - Windows errno variable declaration
 * - Safe redefinition guards (only defines if not already defined)
 * - Header inclusion order safety (must be included before Windows headers)
 *
 * WINDOWS COMPATIBILITY:
 * ======================
 * Windows uses WSA errors for socket operations, but many functions
 * also use standard errno. This header ensures standard errno constants
 * are available on Windows for consistent error handling.
 *
 * DEFINED CONSTANTS:
 * ==================
 * This header defines the following errno constants:
 * - EINVAL: Invalid argument
 * - ERANGE: Result out of range
 * - ETIMEDOUT: Operation timed out
 * - EINTR: Interrupted system call
 * - EBADF: Bad file descriptor
 * - EAGAIN: Resource temporarily unavailable
 * - EWOULDBLOCK: Operation would block (alias for EAGAIN on Windows)
 * - EPIPE: Broken pipe
 * - ECONNREFUSED: Connection refused
 * - ENETUNREACH: Network unreachable
 * - EHOSTUNREACH: Host unreachable
 * - ECONNRESET: Connection reset by peer
 * - ENOTSOCK: Socket operation on non-socket
 *
 * @note This header must be included before any Windows headers that
 *       might define or use these constants.
 * @note All constants are protected with #ifndef guards to prevent
 *       redefinition errors.
 * @note On POSIX systems, these constants are typically defined in
 *       <errno.h>, but this header can still be included safely.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

// Windows errno compatibility definitions
// These constants are not defined in Windows headers by default
// This header must be included before any Windows headers

// Only define errno constants if they're not already defined by the system
#ifndef EINVAL
#define EINVAL 22
#endif

#ifndef ERANGE
#define ERANGE 34
#endif

#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif

#ifndef EINTR
#define EINTR 4
#endif

#ifndef EBADF
#define EBADF 9
#endif

#ifndef EAGAIN
#define EAGAIN 11
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK 11
#endif

#ifndef EPIPE
#define EPIPE 32
#endif

#ifndef ECONNREFUSED
#define ECONNREFUSED 111
#endif

#ifndef ENETUNREACH
#define ENETUNREACH 101
#endif

#ifndef EHOSTUNREACH
#define EHOSTUNREACH 113
#endif

#ifndef ECONNRESET
#define ECONNRESET 104
#endif

#ifndef ENOTSOCK
#define ENOTSOCK 88
#endif

// Declare errno variable for Windows CRT compatibility
extern int errno;

/** @} */
