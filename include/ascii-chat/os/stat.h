#pragma once

/**
 * @file platform/stat.h
 * @brief Cross-platform stat and file type checking macros
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides unified macros for checking file types and properties across
 * Windows and POSIX platforms. Windows requires special handling since
 * it doesn't provide standard POSIX stat macros.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>

/**
 * @brief Check if a file mode represents a regular file (Windows)
 *
 * Windows defines file modes differently from POSIX, so we need to
 * provide a compatibility macro that works the same way.
 */
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

/**
 * @brief Check if a file mode represents a directory (Windows)
 *
 * Windows defines file modes differently from POSIX, so we need to
 * provide a compatibility macro that works the same way.
 */
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#else
/* POSIX platforms - these macros are already defined in <sys/stat.h> */
#include <unistd.h>
#endif

/** @} */
