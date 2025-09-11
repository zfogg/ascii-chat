/**
 * @file system.c
 * @brief Windows system functions implementation for ASCII-Chat platform abstraction layer
 * 
 * This file provides Windows system function wrappers for the platform abstraction layer,
 * enabling cross-platform system operations using a unified API.
 */

#ifdef _WIN32

#include "../abstraction.h"
#include "../internal.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/**
 * @brief Get username from environment variables
 * @return Username string or "unknown" if not found
 */
const char *get_username_env(void) {
    static char username[256];
    const char *user = getenv("USERNAME");
    if (!user) {
        user = getenv("USER");
    }
    if (user) {
        strncpy(username, user, sizeof(username) - 1);
        username[sizeof(username) - 1] = '\0';
        return username;
    }
    return "unknown";
}

/**
 * @brief Initialize platform-specific functionality
 * @return 0 on success, error code on failure
 */
int platform_init(void) {
    // Set binary mode for stdin/stdout to handle raw data
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);

    // Initialize Winsock will be done in socket_windows.c
    return 0;
}

/**
 * @brief Clean up platform-specific functionality
 */
void platform_cleanup(void) {
    // Cleanup will be done in socket_windows.c for Winsock
}

/**
 * @brief Sleep for specified milliseconds
 * @param ms Number of milliseconds to sleep
 */
void platform_sleep_ms(unsigned int ms) {
    Sleep(ms);
}

/**
 * @brief Sleep for specified microseconds
 * @param us Number of microseconds to sleep
 * @note Windows Sleep only supports milliseconds, so we convert
 */
void platform_sleep_us(unsigned int us) {
    // Windows Sleep only supports milliseconds, so convert
    Sleep((us + 999) / 1000);
}

/**
 * @brief POSIX-compatible usleep function for Windows
 * @param usec Number of microseconds to sleep
 * @return 0 on success
 */
int usleep(unsigned int usec) {
    // Use the platform function
    platform_sleep_us(usec);
    return 0;
}

/**
 * @brief Get current process ID
 * @return Process ID as integer
 */
int platform_get_pid(void) {
    return (int)GetCurrentProcessId();
}

/**
 * @brief Get current username
 * @return Username string or "unknown" if not found
 */
const char *platform_get_username(void) {
    return get_username_env();
}

/**
 * @brief Set signal handler
 * @param sig Signal number
 * @param handler Signal handler function
 * @return Previous signal handler, or SIG_ERR on error
 */
signal_handler_t platform_signal(int sig, signal_handler_t handler) {
    return signal(sig, handler);
}

/**
 * @brief Get environment variable value
 * @param name Environment variable name
 * @return Variable value or NULL if not found
 */
const char *platform_getenv(const char *name) {
    return getenv(name);
}

/**
 * @brief Set environment variable
 * @param name Environment variable name
 * @param value Environment variable value
 * @return 0 on success, error code on failure
 */
int platform_setenv(const char *name, const char *value) {
    return _putenv_s(name, value);
}

/**
 * @brief Check if file descriptor is a TTY
 * @param fd File descriptor to check
 * @return 1 if TTY, 0 if not
 */
int platform_isatty(int fd) {
    return _isatty(fd);
}

/**
 * @brief Get TTY device path
 * @return Path to TTY device
 */
const char *platform_get_tty_path(void) {
    return get_tty_path();
}

/**
 * @brief Open TTY device
 * @param mode Open mode string (unused on Windows)
 * @return File descriptor on success, -1 on failure
 */
int platform_open_tty(const char *mode) {
    (void)mode; // Unused on Windows
    // On Windows, we use CON for console access
    return _open("CON", _O_RDWR);
}

/**
 * @brief clock_gettime implementation for Windows
 * @param clk_id Clock ID (unused)
 * @param tp Pointer to timespec structure to fill
 * @return 0 on success, -1 on failure
 */
int clock_gettime(int clk_id, struct timespec *tp) {
    LARGE_INTEGER freq, counter;
    (void)clk_id; // Unused parameter

    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter)) {
        return -1;
    }

    // Convert to seconds and nanoseconds
    tp->tv_sec = counter.QuadPart / freq.QuadPart;
    tp->tv_nsec = ((counter.QuadPart % freq.QuadPart) * 1000000000) / freq.QuadPart;

    return 0;
}

/**
 * @brief aligned_alloc implementation for Windows
 * @param alignment Memory alignment requirement
 * @param size Size of memory block to allocate
 * @return Pointer to aligned memory block, or NULL on failure
 */
void *aligned_alloc(size_t alignment, size_t size) {
    return _aligned_malloc(size, alignment);
}

/**
 * @brief gmtime_r implementation for Windows (thread-safe gmtime)
 * @param timep Pointer to time_t value
 * @param result Pointer to struct tm to fill
 * @return Pointer to result on success, NULL on failure
 */
struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    errno_t err = gmtime_s(result, timep);
    if (err != 0) {
        return NULL;
    }
    return result;
}

#endif // _WIN32