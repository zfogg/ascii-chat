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
#include "../../common.h"
#include "../../asciichat_errno.h"
#include "../socket.h"
#include "../../util/path.h"
#include <dbghelp.h>
#include <wincrypt.h>
#include <io.h>

#include <fcntl.h>
#include <process.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <mmsystem.h> // For timeBeginPeriod/timeEndPeriod (Windows Multimedia API)

#include <stdatomic.h>

#pragma comment(lib, "winmm.lib") // Link Windows Multimedia library for timeBeginPeriod/timeEndPeriod

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
asciichat_error_t platform_init(void) {
  // Set binary mode for stdin/stdout to handle raw data
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);

  // Install crash handlers for automatic backtrace on crashes
  platform_install_crash_handler();

  // Set Windows timer resolution to 1ms for high-precision sleep
  // This is required for multimedia applications that need accurate timing
  // Without this, Sleep(1) can sleep up to 15.6ms (default Windows timer resolution)
  // With timeBeginPeriod(1), Sleep(1) sleeps 1-2ms which is acceptable for 144 FPS capture
  timeBeginPeriod(1);

  // Initialize Winsock (required before getaddrinfo and socket operations)
  if (socket_init() != 0) {
    return SET_ERRNO_SYS(ERROR_NETWORK, "Network operation failed");
  }

  return 0;
}

/**
 * @brief Clean up platform-specific functionality
 */
void platform_cleanup(void) {
  socket_cleanup();

  // Restore original Windows timer resolution
  timeEndPeriod(1);
}

/**
 * @brief Sleep for specified milliseconds
 * @param ms Number of milliseconds to sleep
 */
void platform_sleep_ms(unsigned int ms) {
  Sleep(ms);
}

asciichat_error_t platform_localtime(const time_t *timer, struct tm *result) {
  if (!timer || !result) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for localtime");
  }
  errno_t err = localtime_s(result, timer);
  if (err != 0) {
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Platform initialization failed");
  }
  return ASCIICHAT_OK;
}

/**
 * @brief POSIX-compatible usleep function for Windows
 * @param usec Number of microseconds to sleep
 * @return 0 on success
 */
int usleep(unsigned int usec) {
  // Convert microseconds to milliseconds, minimum 1ms
  int timeout_ms = (int)(usec / 1000);
  if (timeout_ms == 0 && usec > 0) {
    timeout_ms = 1; // Minimum 1ms on Windows
  }
  Sleep(timeout_ms);
  return 0;
}

/**
 * @brief Cross-platform high-precision sleep function with shutdown support
 * @param usec Number of microseconds to sleep
 *
 * On Windows, uses Sleep() with millisecond precision after calling timeBeginPeriod(1).
 * With 1ms timer resolution enabled in platform_init(), Sleep(1) sleeps 1-2ms
 * instead of the default 15.6ms, enabling proper frame rate limiting for 144 FPS capture.
 */
void platform_sleep_usec(unsigned int usec) {
  // Convert microseconds to milliseconds, minimum 1ms
  int timeout_ms = (int)(usec / 1000);
  if (timeout_ms < 1)
    timeout_ms = 1;

  Sleep(timeout_ms);
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
 * @brief Set signal handler (Windows implementation)
 * @param sig Signal number
 * @param handler Signal handler function
 * @return Previous signal handler, or SIG_ERR on error
 * @note Windows signal() is thread-safe, unlike POSIX signal()
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
 * @brief Get TTY name for a file descriptor
 * @param fd File descriptor
 * @return TTY name or NULL if not a TTY
 */
const char *platform_ttyname(int fd) {
  // Windows doesn't have ttyname, return "CON" for console
  if (_isatty(fd)) {
    return "CON";
  }
  return NULL;
}

/**
 * @brief Synchronize file descriptor to disk
 * @param fd File descriptor to sync
 * @return 0 on success, -1 on failure
 */
int platform_fsync(int fd) {
  // Windows uses _commit for file sync
  return _commit(fd);
}

// ============================================================================
// Debug/Stack Trace Functions
// ============================================================================

/**
 * @brief Get stack trace addresses using Windows StackWalk64 API
 * @param buffer Array to store trace addresses
 * @param size Maximum number of addresses to retrieve
 * @return Number of addresses retrieved
 */
int platform_backtrace(void **buffer, int size) {
  if (!buffer || size <= 0) {
    return 0;
  }

  // Try the simpler CaptureStackBackTrace first (faster and more reliable)
  USHORT captured = CaptureStackBackTrace(1, size, buffer, NULL);
  if (captured > 0) {
    return (int)captured;
  }

  // Fall back to the more complex StackWalk64 approach
  // Capture current context
  CONTEXT context;
  RtlCaptureContext(&context);

  // Initialize symbol handler for current process
  HANDLE process = GetCurrentProcess();
  if (!SymInitialize(process, NULL, TRUE)) {
    DWORD error = GetLastError();
    log_error("[ERROR] platform_backtrace: SymInitialize failed with error %lu", error);
    return 0;
  }

  // Set up stack frame based on architecture
  STACKFRAME64 frame;
  memset(&frame, 0, sizeof(frame));

#ifdef _M_IX86
  frame.AddrPC.Offset = context.Eip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Esp;
  frame.AddrStack.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Ebp;
  frame.AddrFrame.Mode = AddrModeFlat;
#elif _M_X64
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
#else
  // Unsupported architecture
  SymCleanup(process);
  return 0;
#endif

  int count = 0;
  while (count < size) {
    BOOL result = StackWalk64(
#ifdef _M_IX86
        IMAGE_FILE_MACHINE_I386,
#else
        IMAGE_FILE_MACHINE_AMD64,
#endif
        process, GetCurrentThread(), &frame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);

    if (!result) {
      DWORD error = GetLastError();
      if (count == 0) {
        log_error("[ERROR] platform_backtrace: StackWalk64 failed with error %lu", error);
      }
      break;
    }

    if (frame.AddrPC.Offset == 0) {
      break;
    }

    buffer[count++] = (void *)(uintptr_t)frame.AddrPC.Offset;
  }

  SymCleanup(process);
  return count;
}

// Global variables for Windows symbol resolution
static atomic_bool g_symbols_initialized = false;
static HANDLE g_process_handle = NULL;

// Forward declaration
static void cleanup_windows_symbols(void);

// Function to initialize Windows symbol resolution once
static void init_windows_symbols(void) {
  if (atomic_load(&g_symbols_initialized)) {
    return; // Already initialized
  }

  // Use a simple mutex to prevent race conditions
  static volatile LONG init_lock = 0;
  if (InterlockedCompareExchange(&init_lock, 1, 0) != 0) {
    // Another thread is initializing, wait for it to complete
    while (!atomic_load(&g_symbols_initialized)) {
      Sleep(1);
    }
    return;
  }

  g_process_handle = GetCurrentProcess();

  // Set symbol options for better debugging
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_AUTO_PUBLICS);

  // Configure symbol search path to include current directory and build directory
  char symbol_path[MAX_PATH * 3];
  char module_path[MAX_PATH];
  char project_root[MAX_PATH];
  DWORD path_len = GetModuleFileNameA(NULL, module_path, MAX_PATH);

  if (path_len > 0) {
    // Get directory of executable
    char *last_slash = strrchr(module_path, '\\');
    if (last_slash) {
      *last_slash = '\0'; // Truncate to directory path
    }

    // Copy the directory path to project_root before further modifications
    strncpy(project_root, module_path, sizeof(project_root) - 1);
    project_root[sizeof(project_root) - 1] = '\0';

    // If we're in build/bin, go up two levels to get to project root
    if (strstr(module_path, "\\build\\bin") != NULL) {
      // Remove \build\bin from the path to get project root
      char *build_bin = strstr(project_root, "\\build\\bin");
      if (build_bin) {
        *build_bin = '\0';
      }
    }

    // Set up symbol search path: current dir, build dir, and system paths
    safe_snprintf(symbol_path, sizeof(symbol_path), "%s;%s\\build\\bin;%s\\build;%s",
             module_path, project_root, project_root, getenv("_NT_SYMBOL_PATH") ? getenv("_NT_SYMBOL_PATH") : "");

    log_debug("Setting symbol search path: %s", symbol_path);
  }

  // Try to initialize with custom search path
  if (!SymInitialize(g_process_handle, symbol_path, TRUE)) {
    // If initialization failed, try to cleanup and retry with default path
    SymCleanup(g_process_handle);
    if (!SymInitialize(g_process_handle, NULL, TRUE)) {
      DWORD error = GetLastError();
      log_error("Failed to initialize Windows symbol system, error: %lu", error);
      return;
    }
  }

  // Load symbols for the current module
  path_len = GetModuleFileNameA(NULL, module_path, MAX_PATH);
  if (path_len > 0) {
    DWORD64 base_addr = SymLoadModule64(g_process_handle, NULL, module_path, NULL, 0, 0);
    if (base_addr == 0) {
      DWORD error = GetLastError();
      log_error("Failed to load symbols for module %s, error: %lu", module_path, error);
    } else {
      log_debug("Successfully loaded symbols for module %s at base 0x%llx", module_path, base_addr);
    }

    // Also try to load symbols from the build directory where PDB files are located
    char build_pdb_path[MAX_PATH];
    char *last_slash = strrchr(module_path, '\\');
    if (last_slash) {
      size_t dir_len = last_slash - module_path;
      if (dir_len < MAX_PATH - 20) { // Leave room for "\build\bin\*.pdb"
        strncpy(build_pdb_path, module_path, dir_len);
        build_pdb_path[dir_len] = '\0';
        strcat(build_pdb_path, "\\build\\bin");

        // Try to load PDB files from build directory
        WIN32_FIND_DATAA find_data;
        char search_pattern[MAX_PATH];
        safe_snprintf(search_pattern, sizeof(search_pattern), "%s\\*.pdb", build_pdb_path);

        HANDLE find_handle = FindFirstFileA(search_pattern, &find_data);
        if (find_handle != INVALID_HANDLE_VALUE) {
          do {
            char full_pdb_path[MAX_PATH];
            safe_snprintf(full_pdb_path, sizeof(full_pdb_path), "%s\\%s", build_pdb_path, find_data.cFileName);
            log_debug("Attempting to load PDB: %s", full_pdb_path);
            // Note: SymLoadModule64 expects executable files, not PDB files directly
            // The PDB should be automatically found by the symbol system
          } while (FindNextFileA(find_handle, &find_data));
          FindClose(find_handle);
        }
      }
    }
  }

  atomic_store(&g_symbols_initialized, true);
  // Register cleanup function to be called on exit
  (void)atexit(cleanup_windows_symbols);
}

// Function to cleanup Windows symbol resolution
static void cleanup_windows_symbols(void) {
  if (atomic_load(&g_symbols_initialized) && g_process_handle) {
    SymCleanup(g_process_handle);
    atomic_store(&g_symbols_initialized, false);
    g_process_handle = NULL;
  }
}

static void resolve_windows_symbol(void *addr, char *buffer, size_t buffer_size) {
  if (!atomic_load(&g_symbols_initialized)) {
    safe_snprintf(buffer, buffer_size, "0x%llx", (DWORD64)(uintptr_t)addr);
    return;
  }

  DWORD64 address = (DWORD64)(uintptr_t)addr;

  // Allocate symbol info structure with space for name (large buffer for very long C++ symbols)
  PSYMBOL_INFO symbol_info = SAFE_MALLOC(sizeof(SYMBOL_INFO) + 4096, PSYMBOL_INFO);
  if (!symbol_info) {
    safe_snprintf(buffer, buffer_size, "0x%llx", address);
    return;
  }
  symbol_info->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol_info->MaxNameLen = 4095;

  // Line info structure
  IMAGEHLP_LINE64 line_info;
  line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
  DWORD displacement = 0;

  // Try to get symbol name and line info
  bool got_symbol = SymFromAddr(g_process_handle, address, NULL, symbol_info);
  bool got_line = SymGetLineFromAddr64(g_process_handle, address, &displacement, &line_info);

  /**
  // Debug logging for symbol resolution
  if (!got_symbol) {
    DWORD error = GetLastError();
    log_debug("SymFromAddr failed for address 0x%llx, error: %lu", address, error);
  }
  if (!got_line) {
    DWORD error = GetLastError();
    log_debug("SymGetLineFromAddr64 failed for address 0x%llx, error: %lu", address, error);
  }
  */

  if (got_symbol && got_line) {
    // Use the existing project relative path extraction function
    const char *filename = extract_project_relative_path(line_info.FileName);

    // Ensure symbol name is null-terminated within our buffer
    symbol_info->Name[symbol_info->MaxNameLen - 1] = '\0';

    // Calculate required space and truncate symbol name if needed
    size_t symbol_len = strlen(symbol_info->Name);
    size_t filename_len = strlen(filename);
    size_t required = symbol_len + filename_len + 32; // extra space for " (:%lu)"

    if (required > buffer_size) {
      // Truncate symbol name to fit
      size_t max_symbol_len = buffer_size - filename_len - 32;
      if (max_symbol_len > 0) {
        safe_snprintf(buffer, buffer_size, "%.*s... (%s:%lu)", (int)(max_symbol_len - 3), symbol_info->Name, filename,
                      line_info.LineNumber);
      } else {
        safe_snprintf(buffer, buffer_size, "0x%llx", address);
      }
    } else {
      safe_snprintf(buffer, buffer_size, "%s (%s:%lu)", symbol_info->Name, filename, line_info.LineNumber);
    }
  } else if (got_symbol) {
    // Ensure symbol name is null-terminated within our buffer
    symbol_info->Name[symbol_info->MaxNameLen - 1] = '\0';

    // Calculate required space and truncate if needed
    size_t symbol_len = strlen(symbol_info->Name);
    size_t required = symbol_len + 32; // extra space for "+0x%llx"

    if (required > buffer_size) {
      // Check for buffer_size >= 35 to safely subtract 32 and then 3 more
      if (buffer_size >= 35) {
        size_t max_symbol_len = buffer_size - 32;
        // Check for underflow before subtraction
        if (address >= symbol_info->Address) {
          safe_snprintf(buffer, buffer_size, "%.*s...+0x%llx", (int)(max_symbol_len - 3), symbol_info->Name,
                        address - symbol_info->Address);
        } else {
          safe_snprintf(buffer, buffer_size, "%.*s...-0x%llx", (int)(max_symbol_len - 3), symbol_info->Name,
                        symbol_info->Address - address);
        }
      } else {
        safe_snprintf(buffer, buffer_size, "0x%llx", address);
      }
    } else {
      // Check for underflow before subtraction
      if (address >= symbol_info->Address) {
        safe_snprintf(buffer, buffer_size, "%s+0x%llx", symbol_info->Name, address - symbol_info->Address);
      } else {
        safe_snprintf(buffer, buffer_size, "%s-0x%llx", symbol_info->Name, symbol_info->Address - address);
      }
    }
  } else {
    safe_snprintf(buffer, buffer_size, "0x%llx", address);
  }

  SAFE_FREE(symbol_info);
}

/**
 * @brief Convert stack trace addresses to symbols using Windows SymFromAddr
 * @param buffer Array of addresses from platform_backtrace
 * @param size Number of addresses in buffer
 * @return Array of strings with symbol names (must be freed with platform_backtrace_symbols_free)
 */
char **platform_backtrace_symbols(void *const *buffer, int size) {
  if (!buffer || size <= 0) {
    return NULL;
  }

  // Initialize Windows symbols once
  init_windows_symbols();

  // Allocate array of strings (size + 1 for NULL terminator)
  char **symbols = SAFE_MALLOC((size + 1) * sizeof(char *), char **);
  if (!symbols) {
    return NULL;
  }

  // Initialize all pointers to NULL
  for (int i = 0; i <= size; i++) {
    symbols[i] = NULL;
  }

  // Resolve each symbol
  for (int i = 0; i < size; i++) {
    symbols[i] = (char *)SAFE_MALLOC(1024, void *); // Increased buffer size for longer symbol names
    if (symbols[i]) {
      resolve_windows_symbol(buffer[i], symbols[i], 1024);
    }
  }

  return symbols;
}

/**
 * @brief Free memory from platform_backtrace_symbols
 * @param strings Array returned by platform_backtrace_symbols
 */
void platform_backtrace_symbols_free(char **strings) {
  if (!strings) {
    return;
  }

  // Free each individual string
  for (int i = 0; strings[i] != NULL; i++) {
    SAFE_FREE(strings[i]);
  }

  // Free the array itself
  SAFE_FREE(strings);
}

// ============================================================================
// Crash Handling
// ============================================================================

/**
 * @brief Print backtrace to stderr
 * @param skip_frames Number of additional frames to skip (beyond platform_print_backtrace itself)
 */
void platform_print_backtrace(int skip_frames) {
  void *buffer[32];
  int size = platform_backtrace(buffer, 32);

  if (size > 0) {
    safe_fprintf(stderr, "\n=== BACKTRACE ===\n");
    char **symbols = platform_backtrace_symbols(buffer, size);

    // Skip platform_print_backtrace itself (1 frame) + any additional frames requested
    int start_frame = 1 + skip_frames;
    for (int i = start_frame; i < size; i++) {
      safe_fprintf(stderr, "%2d: %s\n", i - start_frame, symbols ? symbols[i] : "???");
    }

    platform_backtrace_symbols_free(symbols);
    safe_fprintf(stderr, "================\n\n");
  }
}

/**
 * @brief Windows structured exception handler for crashes
 */
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *exception_info) {
  safe_fprintf(stderr, "\n*** CRASH DETECTED ***\n");
  safe_fprintf(stderr, "Exception Code: 0x%08lx\n", exception_info->ExceptionRecord->ExceptionCode);

  switch (exception_info->ExceptionRecord->ExceptionCode) {
  case EXCEPTION_ACCESS_VIOLATION:
    safe_fprintf(stderr, "Exception: Access Violation (SIGSEGV)\n");
    break;
  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    safe_fprintf(stderr, "Exception: Array Bounds Exceeded\n");
    break;
  case EXCEPTION_DATATYPE_MISALIGNMENT:
    safe_fprintf(stderr, "Exception: Data Type Misalignment\n");
    break;
  case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    safe_fprintf(stderr, "Exception: Floating Point Divide by Zero (SIGFPE)\n");
    break;
  case EXCEPTION_FLT_INVALID_OPERATION:
    safe_fprintf(stderr, "Exception: Floating Point Invalid Operation (SIGFPE)\n");
    break;
  case EXCEPTION_ILLEGAL_INSTRUCTION:
    safe_fprintf(stderr, "Exception: Illegal Instruction (SIGILL)\n");
    break;
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
    safe_fprintf(stderr, "Exception: Integer Divide by Zero (SIGFPE)\n");
    break;
  case EXCEPTION_STACK_OVERFLOW:
    safe_fprintf(stderr, "Exception: Stack Overflow\n");
    break;
  default:
    safe_fprintf(stderr, "Exception: Unknown (0x%08lx)\n", exception_info->ExceptionRecord->ExceptionCode);
    break;
  }

#ifndef NDEBUG
  // Only capture backtraces in Debug builds
  platform_print_backtrace(0);
#else
  fprintf(stderr, "Backtrace disabled in Release builds\n");
#endif

  // Return EXCEPTION_EXECUTE_HANDLER to terminate the program
  return EXCEPTION_EXECUTE_HANDLER;
}

/**
 * @brief Signal handler for Windows C runtime exceptions
 */
static void windows_signal_handler(int sig) {
  safe_fprintf(stderr, "\n*** CRASH DETECTED ***\n");
  switch (sig) {
  case SIGABRT:
    safe_fprintf(stderr, "Signal: SIGABRT (Abort)\n");
    break;
  case SIGFPE:
    safe_fprintf(stderr, "Signal: SIGFPE (Floating Point Exception)\n");
    break;
  case SIGILL:
    safe_fprintf(stderr, "Signal: SIGILL (Illegal Instruction)\n");
    break;
  default:
    safe_fprintf(stderr, "Signal: %d (Unknown)\n", sig);
    break;
  }

#ifndef NDEBUG
  // Only capture backtraces in Debug builds
  platform_print_backtrace(0);
#else
  fprintf(stderr, "Backtrace disabled in Release builds\n");
#endif

  exit(1);
}

/**
 * @brief Install crash handlers for Windows
 */
void platform_install_crash_handler(void) {
  // Install structured exception handler
  SetUnhandledExceptionFilter(crash_handler);

  // Also install signal handlers for C runtime exceptions
  platform_signal(SIGABRT, windows_signal_handler);
  platform_signal(SIGFPE, windows_signal_handler);
  platform_signal(SIGILL, windows_signal_handler);
}

/**
 * @brief clock_gettime implementation for Windows
 * @param clk_id Clock ID (CLOCK_REALTIME or CLOCK_MONOTONIC)
 * @param tp Pointer to timespec structure to fill
 * @return 0 on success, -1 on failure
 */
int clock_gettime(int clk_id, struct timespec *tp) {
  if (!tp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid timespec pointer");
  }

  if (clk_id == CLOCK_REALTIME) {
    // Get current wall clock time (Unix epoch time)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    // Convert to 64-bit value
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;

// Windows file time is 100ns intervals since January 1, 1601
// Unix epoch is January 1, 1970
// The difference is 11644473600 seconds
#define WINDOWS_TICK 10000000ULL
#define SEC_TO_UNIX_EPOCH 11644473600ULL

    tp->tv_sec = (time_t)((ull.QuadPart / WINDOWS_TICK) - SEC_TO_UNIX_EPOCH);
    tp->tv_nsec = (long)((ull.QuadPart % WINDOWS_TICK) * 100);
  } else {
    // For CLOCK_MONOTONIC or other clocks, use QueryPerformanceCounter
    LARGE_INTEGER freq, counter;

    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter)) {
      return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Platform initialization failed");
    }

    // Convert to seconds and nanoseconds
    tp->tv_sec = counter.QuadPart / freq.QuadPart;
    tp->tv_nsec = ((counter.QuadPart % freq.QuadPart) * 1000000000) / freq.QuadPart;
  }

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

// ============================================================================
// Memory Operations
// ============================================================================

/**
 * @brief Allocate aligned memory
 * @param alignment Alignment requirement (must be power of 2)
 * @param size Size of memory block
 * @return Pointer to aligned memory, or NULL on failure
 */
void *platform_aligned_alloc(size_t alignment, size_t size) {
  return _aligned_malloc(size, alignment);
}

/**
 * @brief Free aligned memory
 * @param ptr Pointer to aligned memory block
 */
void platform_aligned_free(void *ptr) {
  _aligned_free(ptr);
}

/**
 * @brief Memory barrier for synchronization
 */
void platform_memory_barrier(void) {
  MemoryBarrier();
}

// ============================================================================
// Error Handling
// ============================================================================

// Thread-local storage for error strings
static __declspec(thread) char error_buffer[256];

/**
 * @brief Get thread-safe error string
 * @param errnum Error number
 * @return Error string (thread-local storage)
 */
const char *platform_strerror(int errnum) {
  (void)strerror_s(error_buffer, sizeof(error_buffer), errnum);
  return error_buffer;
}

/**
 * @brief Get last error code
 * @return Last error code (GetLastError on Windows)
 */
int platform_get_last_error(void) {
  return (int)GetLastError();
}

/**
 * @brief Set last error code
 * @param error Error code to set
 */
void platform_set_last_error(int error) {
  SetLastError((DWORD)error);
}

// ============================================================================
// File Operations
// ============================================================================

/**
 * @brief Open file with platform-safe flags
 * @param pathname File path
 * @param flags Open flags
 * @param ... Mode (if O_CREAT is specified)
 * @return File descriptor on success, -1 on failure
 */
int platform_open(const char *pathname, int flags, ...) {
  int mode = 0;
  if (flags & _O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
  }

  // Windows requires _O_BINARY for binary mode
  if (!(flags & _O_TEXT)) {
    flags |= _O_BINARY;
  }

  return _open(pathname, flags, mode);
}

/**
 * @brief Open file descriptor with platform-safe mode
 * @param fd File descriptor
 * @param mode File mode
 * @return File pointer on success, NULL on failure
 */
FILE *platform_fdopen(int fd, const char *mode) {
  return _fdopen(fd, mode);
}

/**
 * @brief Read from file descriptor
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
ssize_t platform_read(int fd, void *buf, size_t count) {
  // Windows _read returns int, convert to ssize_t
  return (ssize_t)_read(fd, buf, (unsigned int)count);
}

/**
 * @brief Write to file descriptor
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
ssize_t platform_write(int fd, const void *buf, size_t count) {
  // Use Windows WriteFile API to avoid deprecated _write
  HANDLE handle = (HANDLE)_get_osfhandle(fd);
  if (handle == INVALID_HANDLE_VALUE) {
    return -1;
  }

  DWORD bytes_written = 0;
  if (WriteFile(handle, buf, (DWORD)count, &bytes_written, NULL)) {
    return (ssize_t)bytes_written;
  }

  return -1;
}

/**
 * @brief Close file descriptor
 * @param fd File descriptor
 * @return 0 on success, -1 on failure
 */
int platform_close(int fd) {
  return _close(fd);
}

/**
 * @brief Open file with platform-safe mode
 * @param filename File path
 * @param mode File mode (e.g., "r", "w", "a", "rb", "wb")
 * @return File pointer on success, NULL on failure
 */
FILE *platform_fopen(const char *filename, const char *mode) {
  return fopen(filename, mode);
}

/**
 * @brief Delete file
 * @param pathname File path
 * @return 0 on success, -1 on failure
 */
int platform_unlink(const char *pathname) {
  return _unlink(pathname);
}

/**
 * @brief Change file permissions
 * @param pathname File path
 * @param mode Permission mode (e.g., _S_IREAD, _S_IWRITE, _S_IREAD | _S_IWRITE)
 * @return 0 on success, -1 on failure
 * @note Windows has limited permission support compared to POSIX
 */
int platform_chmod(const char *pathname, int mode) {
  return _chmod(pathname, mode);
}

// ============================================================================
// Safe Memory Functions
// ============================================================================

asciichat_error_t platform_memcpy(void *dest, size_t dest_size, const void *src, size_t count) {
  // Validate parameters
  if (!dest || !src) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid pointers for memcpy");
  }

  if (count > dest_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer overflow protection: count=%zu > dest_size=%zu", count, dest_size);
  }

#ifdef __STDC_LIB_EXT1__
  // Use memcpy_s if available (C11 Annex K)
  errno_t err = memcpy_s(dest, dest_size, src, count);
  if (err != 0) {
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Platform initialization failed");
  }
  return ASCIICHAT_OK;
#else
  // Fallback to standard memcpy with bounds already checked
  memcpy(dest, src, count);
  return ASCIICHAT_OK;
#endif
}

asciichat_error_t platform_memset(void *dest, size_t dest_size, int ch, size_t count) {
  // Validate parameters
  if (!dest) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid pointer for memset");
    return ERROR_INVALID_PARAM;
  }

  if (count > dest_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer overflow protection: count=%zu > dest_size=%zu", count, dest_size);
  }

#ifdef __STDC_LIB_EXT1__
  // Use memset_s if available (C11 Annex K)
  errno_t err = memset_s(dest, dest_size, ch, count);
  return err; // Returns 0 on success, non-zero on error
#else
  // Fallback to standard memset with bounds already checked
  memset(dest, ch, count);
  return 0; // Success
#endif
}

int platform_memmove(void *dest, size_t dest_size, const void *src, size_t count) {
  // Validate parameters
  if (!dest || !src) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid pointers for memmove");
  }

  if (count > dest_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer overflow protection: count=%zu > dest_size=%zu", count, dest_size);
  }

#ifdef __STDC_LIB_EXT1__
  // Use memmove_s if available (C11 Annex K)
  errno_t err = memmove_s(dest, dest_size, src, count);
  return err; // Returns 0 on success, non-zero on error
#else
  // Fallback to standard memmove with bounds already checked
  memmove(dest, src, count);
  return 0; // Success
#endif
}

/**
 * Resolve hostname to IPv4 address (Windows implementation)
 *
 * Performs DNS resolution to convert a hostname to an IPv4 address string.
 * Handles Windows-specific Winsock initialization and cleanup.
 *
 * @param hostname Hostname to resolve (e.g., "example.com")
 * @param ipv4_out Buffer to store the resolved IPv4 address (e.g., "192.168.1.1")
 * @param ipv4_out_size Size of the output buffer
 * @return 0 on success, -1 on failure
 */
int platform_resolve_hostname_to_ipv4(const char *hostname, char *ipv4_out, size_t ipv4_out_size) {
  if (!hostname || !ipv4_out || ipv4_out_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for hostname resolution");
  }

  // Initialize Winsock on Windows (required for getaddrinfo)
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    return SET_ERRNO_SYS(ERROR_NETWORK, "Network operation failed");
  }

  struct addrinfo hints, *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;       // IPv4 only
  hints.ai_socktype = SOCK_STREAM; // TCP
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  int ret = getaddrinfo(hostname, NULL, &hints, &result);
  if (ret != 0) {
    WSACleanup();
    return SET_ERRNO_SYS(ERROR_NETWORK, "Network operation failed");
  }

  if (!result) {
    freeaddrinfo(result);
    WSACleanup();
    return SET_ERRNO(ERROR_NETWORK, "No address found for hostname: %s", hostname);
  }

  // Extract IPv4 address from first result
  struct sockaddr_in *ipv4_addr = (struct sockaddr_in *)result->ai_addr;
  if (inet_ntop(AF_INET, &(ipv4_addr->sin_addr), ipv4_out, (socklen_t)ipv4_out_size) == NULL) {
    freeaddrinfo(result);
    WSACleanup();
    return SET_ERRNO_SYS(ERROR_NETWORK, "Network operation failed");
  }

  freeaddrinfo(result);
  WSACleanup();

  return 0;
}

/**
 * Load system CA certificates for TLS/HTTPS (Windows implementation)
 *
 * Uses Windows CryptoAPI to extract root CA certificates from the system store
 * and converts them to PEM format for use with BearSSL or other TLS libraries.
 *
 * @param pem_data_out Pointer to receive allocated PEM data (caller must free)
 * @param pem_size_out Pointer to receive size of PEM data
 * @return 0 on success, -1 on failure
 */
asciichat_error_t platform_load_system_ca_certs(char **pem_data_out, size_t *pem_size_out) {
  if (!pem_data_out || !pem_size_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for CA cert loading");
  }

  // Open the Windows system root certificate store
  HCERTSTORE hStore = CertOpenSystemStoreA(0, "ROOT");
  if (!hStore) {
    return SET_ERRNO_SYS(ERROR_CRYPTO, "Crypto operation failed");
  }

  // Allocate buffer for PEM data (start with 256KB, can grow)
  size_t pem_capacity = 256 * 1024;
  size_t pem_size = 0;
  char *pem_data = SAFE_MALLOC(pem_capacity, char *);
  if (!pem_data) {
    CertCloseStore(hStore, 0);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for CA certificates");
  }

  // Enumerate all certificates in the store
  PCCERT_CONTEXT pCertContext = NULL;
  while ((pCertContext = CertEnumCertificatesInStore(hStore, pCertContext)) != NULL) {
    // Convert DER to PEM format (Base64 with headers)
    // Certificate is in pCertContext->pbCertEncoded (DER format)

    // Calculate Base64 size needed (4/3 of input + padding)
    DWORD base64_size = 0;
    if (!CryptBinaryToStringA(pCertContext->pbCertEncoded, pCertContext->cbCertEncoded, CRYPT_STRING_BASE64HEADER, NULL,
                              &base64_size)) {
      continue; // Skip this cert
    }

    // Ensure we have enough space in PEM buffer
    while (pem_size + base64_size + 100 > pem_capacity) {
      pem_capacity *= 2;
      char *new_pem_data = SAFE_REALLOC(pem_data, pem_capacity, char *);
      if (!new_pem_data) {
        SAFE_FREE(pem_data);
        CertFreeCertificateContext(pCertContext);
        CertCloseStore(hStore, 0);
        return SET_ERRNO(ERROR_CRYPTO, "Failed to convert certificate to PEM format");
      }
      pem_data = new_pem_data;
    }

    // Convert DER to Base64 with "-----BEGIN CERTIFICATE-----" header
    DWORD written = (DWORD)(pem_capacity - pem_size);
    if (CryptBinaryToStringA(pCertContext->pbCertEncoded, pCertContext->cbCertEncoded, CRYPT_STRING_BASE64HEADER,
                             pem_data + pem_size, &written)) {
      pem_size += written - 1; // -1 to exclude null terminator added by CryptBinaryToStringA

      // Add newline separator between certificates for proper PEM parsing
      // CryptBinaryToStringA might not always include a trailing newline
      if (pem_size > 0 && pem_data[pem_size - 1] != '\n') {
        pem_data[pem_size++] = '\n';
      }
    }
  }

  CertCloseStore(hStore, 0);

  if (pem_size == 0) {
    SAFE_FREE(pem_data);
    return SET_ERRNO(ERROR_CRYPTO, "No CA certificates found in system store");
  }

  // Null-terminate the PEM data
  pem_data[pem_size] = '\0';

  *pem_data_out = pem_data;
  *pem_size_out = pem_size;
  return 0;
}

#endif // !!_WIN32
