/* Windows compatibility stub for unistd.h
 *
 * Provides POSIX-like definitions for Windows builds.
 * Only included on Windows (POSIX systems use the real unistd.h).
 */
#ifndef _UNISTD_H_COMPAT
#define _UNISTD_H_COMPAT

#ifdef _WIN32

#include <io.h>
#include <process.h>
#include <direct.h>
#include <basetsd.h>

/* Standard file descriptors */
#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

/* ssize_t */
#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

/* Map POSIX functions to Windows equivalents */
#ifndef access
#define access    _access
#endif
#ifndef dup
#define dup       _dup
#endif
#ifndef dup2
#define dup2      _dup2
#endif
#ifndef close
#define close     _close
#endif
#ifndef read
#define read      _read
#endif
#ifndef write
#define write     _write
#endif
#ifndef getcwd
#define getcwd    _getcwd
#endif
#ifndef chdir
#define chdir     _chdir
#endif
#ifndef isatty
#define isatty    _isatty
#endif
#ifndef fileno
#define fileno    _fileno
#endif
#ifndef unlink
#define unlink    _unlink
#endif
#ifndef rmdir
#define rmdir     _rmdir
#endif
#ifndef getpid
#define getpid    _getpid
#endif

/* access() mode flags */
#ifndef F_OK
#define F_OK 0
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 0 /* Windows doesn't have execute permission, map to existence check */
#endif

/* sleep/usleep are handled by ascii-chat/platform/system.h macros */

#endif /* _WIN32 */
#endif /* _UNISTD_H_COMPAT */
