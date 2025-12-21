/**
 * @file platform/windows/terminal.c
 * @ingroup platform
 * @brief 💻 Windows Console API with ANSI color support and capability detection
 */

#ifdef _WIN32

#include "../../options.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>
#include <string.h>
#include <stdatomic.h>

/* ============================================================================
 * Windows Console Resize Detection
 * ============================================================================ */

/**
 * Callback function type for terminal resize events
 * @param cols New terminal width in columns
 * @param rows New terminal height in rows
 */
typedef void (*terminal_resize_callback_t)(int cols, int rows);

/** Global resize callback function pointer */
static terminal_resize_callback_t g_resize_callback = NULL;

/** Thread handle for resize detection */
static asciithread_t g_resize_thread = {0};

/** Flag to signal resize thread should exit */
static atomic_bool g_resize_thread_should_exit = false;

/** Flag indicating if resize detection is active */
static atomic_bool g_resize_detection_active = false;

/**
 * Background thread that monitors for Windows console resize events
 *
 * Uses ReadConsoleInput to detect WINDOW_BUFFER_SIZE_EVENT which is
 * triggered when the console window is resized. This provides equivalent
 * functionality to Unix SIGWINCH signal handling.
 *
 * @param arg Unused thread argument
 * @return NULL on exit
 */
static void *resize_detection_thread(void *arg) {
  (void)arg;
  HANDLE hConsoleInput = GetStdHandle(STD_INPUT_HANDLE);
  if (hConsoleInput == INVALID_HANDLE_VALUE) {
    log_error("Failed to get console input handle for resize detection");
    return NULL;
  }

  // Enable window input events
  DWORD console_mode;
  if (!GetConsoleMode(hConsoleInput, &console_mode)) {
    log_error("Failed to get console mode for resize detection");
    return NULL;
  }

  // Add WINDOW_INPUT flag to enable window buffer size events
  console_mode |= ENABLE_WINDOW_INPUT;
  if (!SetConsoleMode(hConsoleInput, console_mode)) {
    log_error("Failed to enable window input events for resize detection");
    return NULL;
  }

  log_debug("Windows console resize detection thread started");

  // Store last known size to detect actual changes
  CONSOLE_SCREEN_BUFFER_INFO last_csbi = {0};
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &last_csbi)) {
    log_debug("Initial console size: %dx%d", last_csbi.srWindow.Right - last_csbi.srWindow.Left + 1,
              last_csbi.srWindow.Bottom - last_csbi.srWindow.Top + 1);
  }

  while (!atomic_load(&g_resize_thread_should_exit)) {
    INPUT_RECORD input_record;
    DWORD events_read = 0;

    // Wait for console input with timeout (100ms)
    DWORD wait_result = WaitForSingleObject(hConsoleInput, 100);
    if (wait_result == WAIT_TIMEOUT) {
      continue;
    }
    if (wait_result != WAIT_OBJECT_0) {
      continue; // Error or unexpected result
    }

    // Read console input events
    if (!ReadConsoleInput(hConsoleInput, &input_record, 1, &events_read)) {
      continue;
    }

    if (events_read == 0) {
      continue;
    }

    // Check for window buffer size event
    if (input_record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
      CONSOLE_SCREEN_BUFFER_INFO csbi;
      HANDLE hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);

      if (GetConsoleScreenBufferInfo(hConsoleOutput, &csbi)) {
        int new_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int new_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

        // Only trigger callback if size actually changed
        int old_cols = last_csbi.srWindow.Right - last_csbi.srWindow.Left + 1;
        int old_rows = last_csbi.srWindow.Bottom - last_csbi.srWindow.Top + 1;

        if (new_cols != old_cols || new_rows != old_rows) {
          log_debug("Console resized: %dx%d -> %dx%d", old_cols, old_rows, new_cols, new_rows);

          // Update last known size
          last_csbi = csbi;

          // Trigger callback if registered
          if (g_resize_callback != NULL) {
            g_resize_callback(new_cols, new_rows);
          }
        }
      }
    }
  }

  log_debug("Windows console resize detection thread exiting");
  return NULL;
}

/**
 * Start Windows console resize detection thread
 *
 * Spawns a background thread that monitors for console resize events
 * and calls the registered callback function when resizing occurs.
 *
 * @param callback Function to call when resize is detected
 * @return 0 on success, -1 on failure
 */
int terminal_start_resize_detection(terminal_resize_callback_t callback) {
  if (atomic_load(&g_resize_detection_active)) {
    log_warn("Resize detection already active");
    return 0; // Already running
  }

  if (callback == NULL) {
    log_error("Cannot start resize detection with NULL callback");
    return -1;
  }

  g_resize_callback = callback;
  atomic_store(&g_resize_thread_should_exit, false);

  if (ascii_thread_create(&g_resize_thread, resize_detection_thread, NULL) != 0) {
    log_error("Failed to create resize detection thread");
    g_resize_callback = NULL;
    return -1;
  }

  atomic_store(&g_resize_detection_active, true);
  log_info("Windows console resize detection started");
  return 0;
}

/**
 * Stop Windows console resize detection thread
 *
 * Signals the resize detection thread to exit and waits for it to complete.
 */
void terminal_stop_resize_detection(void) {
  if (!atomic_load(&g_resize_detection_active)) {
    return; // Not running
  }

  atomic_store(&g_resize_thread_should_exit, true);
  // Wait for thread to exit
  ascii_thread_join(&g_resize_thread, NULL);
  atomic_store(&g_resize_detection_active, false);
  g_resize_callback = NULL;
}

/**
 * @brief Get terminal size
 * @param size Pointer to terminal_size_t structure to fill
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_get_size(terminal_size_t *size) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

  if (h == INVALID_HANDLE_VALUE) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");
  }

  if (GetConsoleScreenBufferInfo(h, &csbi)) {
    size->cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    size->rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return ASCIICHAT_OK;
  }

  return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");
}

/**
 * @brief Get the TTY device path
 * @return Path to console device ("CON" on Windows)
 */
const char *get_tty_path(void) {
  return "CON";
}

/**
 * @brief Set terminal raw mode
 * @param enable True to enable raw mode, false to restore normal mode
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_set_raw_mode(bool enable) {
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  if (hStdin == INVALID_HANDLE_VALUE) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");
  }

  DWORD mode;
  if (!GetConsoleMode(hStdin, &mode)) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");
  }

  if (enable) {
    mode &= ~((DWORD)ENABLE_LINE_INPUT | (DWORD)ENABLE_ECHO_INPUT);
  } else {
    mode |= (DWORD)((DWORD)ENABLE_LINE_INPUT | (DWORD)ENABLE_ECHO_INPUT);
  }

  if (!SetConsoleMode(hStdin, mode)) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");
  }
  return 0;
}

/**
 * @brief Set terminal echo mode
 * @param enable True to enable echo, false to disable
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_set_echo(bool enable) {
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  if (hStdin == INVALID_HANDLE_VALUE) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");
  }

  DWORD mode;
  if (!GetConsoleMode(hStdin, &mode)) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");
  }

  if (enable) {
    mode |= (DWORD)ENABLE_ECHO_INPUT;
  } else {
    mode &= ~(DWORD)ENABLE_ECHO_INPUT;
  }

  if (!SetConsoleMode(hStdin, mode)) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");
  }
  return 0;
}

/**
 * @brief Check if terminal supports UTF-8 output
 * @return True if UTF-8 is supported, false otherwise
 * @note Checks console code page and terminal type
 */
bool terminal_supports_utf8(void) {
  // Check if console output code page is UTF-8 (65001)
  UINT cp = GetConsoleOutputCP();
  if (cp == 65001) {
    return true;
  }

  // Check for Windows Terminal via environment variable
  const char *wt_session = SAFE_GETENV("WT_SESSION");
  if (wt_session != NULL) {
    return true; // Windows Terminal always supports UTF-8
  }

  // Check for ConEmu
  const char *conemu = SAFE_GETENV("ConEmuPID");
  if (conemu != NULL) {
    return true; // ConEmu supports UTF-8
  }

  // Check for newer Windows Console Host with VT support
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE) {
    DWORD mode;
    if (GetConsoleMode(hOut, &mode)) {
      // ENABLE_VIRTUAL_TERMINAL_PROCESSING (0x0004) indicates modern console
      if (mode & 0x0004) {
        return true; // Modern Windows console with VT support likely has UTF-8
      }
    }
  }

  return false; // Default to no UTF-8 support
}

/**
 * @brief Check if terminal supports color output
 * @return True if color is supported, false otherwise
 * @note Windows 10+ supports ANSI colors
 */
bool terminal_supports_color(void) {
  // Windows 10+ supports ANSI colors
  return true;
}

/**
 * @brief Check if terminal supports Unicode output
 * @return True if Unicode is supported, false otherwise
 * @note Windows supports Unicode through wide character APIs
 */
bool terminal_supports_unicode(void) {
  // Windows supports Unicode through wide character APIs
  return true;
}

/**
 * @brief Clear the terminal screen
 * @return 0 on success, non-zero on failure
 */
asciichat_error_t terminal_clear_screen(void) {
  system("cls");
  return ASCIICHAT_OK;
}

/**
 * @brief Move cursor to specified position
 * @param row Row position (0-based)
 * @param col Column position (0-based)
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_move_cursor(int row, int col) {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hConsole == INVALID_HANDLE_VALUE)
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");

  COORD coord;
  coord.X = (SHORT)col;
  coord.Y = (SHORT)row;

  return SetConsoleCursorPosition(hConsole, coord) ? 0 : -1;
}

/**
 * @brief Enable ANSI escape sequence processing
 * @note Enable ANSI escape sequences on Windows 10+
 */
void terminal_enable_ansi(void) {
  // Enable ANSI escape sequences on Windows 10+
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE) {
    DWORD mode;
    if (GetConsoleMode(hOut, &mode)) {
      mode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
      SetConsoleMode(hOut, mode);
    }
  }
}

// ============================================================================
// Extended Terminal Control
// ============================================================================

/**
 * @brief Set terminal buffering mode
 * @param line_buffered True for line buffering, false for no buffering
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_set_buffering(bool line_buffered) {
  // Windows console doesn't have direct line buffering control
  // This is typically handled at the C runtime level
  if (line_buffered) {
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
  } else {
    (void)setvbuf(stdout, NULL, _IONBF, 0);
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Flush terminal output
 * @param fd File descriptor (ignored on Windows - console APIs use stdout)
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_flush(int fd) {
  (void)fd; // Windows console APIs operate on stdout, not arbitrary file descriptors
  return fflush(stdout);
}

/**
 * @brief Get current cursor position
 * @param row Pointer to store row position
 * @param col Pointer to store column position
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_get_cursor_position(int *row, int *col) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

  if (hOut == INVALID_HANDLE_VALUE) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");
  }

  if (!GetConsoleScreenBufferInfo(hOut, &csbi)) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Terminal operation failed");
  }

  if (row)
    *row = csbi.dwCursorPosition.Y + 1; // Convert to 1-based
  if (col)
    *col = csbi.dwCursorPosition.X + 1; // Convert to 1-based

  return 0;
}

/**
 * @brief Save cursor position (using ANSI if available)
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_save_cursor(void) {
  // Try ANSI escape sequence first (Windows 10+)
  printf("\033[s");
  (void)fflush(stdout);
  return ASCIICHAT_OK;
}

/**
 * @brief Restore cursor position (using ANSI if available)
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_restore_cursor(void) {
  // Try ANSI escape sequence first (Windows 10+)
  printf("\033[u");
  (void)fflush(stdout);
  return 0;
}

/**
 * @brief Set terminal window title
 * @param title New window title
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_set_title(const char *title) {
  if (SetConsoleTitleA(title)) {
    return ASCIICHAT_OK;
  }
  // Fallback to ANSI escape sequence
  printf("\033]0;%s\007", title);
  (void)fflush(stdout);
  return ASCIICHAT_OK;
}

/**
 * @brief Ring terminal bell
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_ring_bell(void) {
  // Use Windows beep
  Beep(800, 200); // 800Hz for 200ms
  return ASCIICHAT_OK;
}

/**
 * @brief Hide or show cursor
 * @param fd File descriptor (ignored on Windows - console APIs use stdout)
 * @param hide True to hide cursor, false to show
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_hide_cursor(int fd, bool hide) {
  (void)fd; // Windows console APIs operate on stdout, not arbitrary file descriptors
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_CURSOR_INFO cursorInfo;

  if (hOut == INVALID_HANDLE_VALUE) {
    // Fallback to ANSI
    printf(hide ? "\033[?25l" : "\033[?25h");
    (void)fflush(stdout);
    return ASCIICHAT_OK;
  }

  if (!GetConsoleCursorInfo(hOut, &cursorInfo)) {
    // If we can't get console cursor info, fall back to ANSI escape sequences
    // This happens in PowerShell or other non-console environments
    printf(hide ? "\033[?25l" : "\033[?25h");
    (void)fflush(stdout);
    return ASCIICHAT_OK;
  }

  cursorInfo.bVisible = !hide;

  if (!SetConsoleCursorInfo(hOut, &cursorInfo)) {
    // If we can't set console cursor info, fall back to ANSI escape sequences
    // This happens in PowerShell or other non-console environments
    printf(hide ? "\033[?25l" : "\033[?25h");
    (void)fflush(stdout);
    return ASCIICHAT_OK;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Set scroll region (using ANSI)
 * @param top Top line of scroll region (1-based)
 * @param bottom Bottom line of scroll region (1-based)
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_set_scroll_region(int top, int bottom) {
  // Use ANSI escape sequence (Windows 10+ with VT processing enabled)
  printf("\033[%d;%dr", top, bottom);
  (void)fflush(stdout);
  return ASCIICHAT_OK;
}

/**
 * @brief Reset terminal to default state
 * @param fd File descriptor for the terminal
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_reset(int fd) {
  // Reset using ANSI escape sequence
  const char *reset_seq = "\033c"; // Full reset
  HANDLE h = (HANDLE)_get_osfhandle(fd);

  if (h != INVALID_HANDLE_VALUE) {
    DWORD written;
    WriteConsole(h, reset_seq, (DWORD)strlen(reset_seq), &written, NULL);
  } else {
    // Fall back to stdout if handle is invalid
    printf("%s", reset_seq);
    (void)fflush(stdout);
  }

  // Also reset Windows console attributes
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE) {
    SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Move cursor to home position (0,0)
 * @param fd File descriptor (ignored on Windows - console APIs use stdout)
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_cursor_home(int fd) {
  (void)fd; // Windows console APIs operate on stdout, not arbitrary file descriptors
  // Use ANSI escape sequence (Windows 10+ with VT processing enabled)
  if (printf("\033[H") < 0) {
    return SET_ERRNO(ERROR_TERMINAL, "Failed to clear screen");
  }
  return fflush(stdout);
}

/**
 * @brief Clear terminal scrollback buffer
 * @param fd File descriptor (ignored on Windows - console APIs use stdout)
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_clear_scrollback(int fd) {
  (void)fd; // Windows console APIs operate on stdout, not arbitrary file descriptors
  // Use ANSI escape sequence (Windows 10+ with VT processing enabled)
  if (printf("\033[3J") < 0) {
    return SET_ERRNO(ERROR_TERMINAL, "Failed to clear screen and scrollback");
  }
  return fflush(stdout);
}

// ============================================================================
// Terminal Detection and Capabilities (Windows)
// ============================================================================

/**
 * Get terminal size with Windows Console API - simpler than Unix
 */
asciichat_error_t get_terminal_size(unsigned short int *width, unsigned short int *height) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE console_handle = GetStdHandle(STD_OUTPUT_HANDLE);

  if (console_handle == INVALID_HANDLE_VALUE) {
    goto fallback;
  }

  if (GetConsoleScreenBufferInfo(console_handle, &csbi)) {
    // Use window size, not buffer size
    *width = (unsigned short int)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
    *height = (unsigned short int)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    return ASCIICHAT_OK;
  }

fallback:
  // Environment variable fallback
  const char *cols_env = SAFE_GETENV("COLUMNS");
  const char *lines_env = SAFE_GETENV("LINES");

  *width = OPT_WIDTH_DEFAULT;
  *height = OPT_HEIGHT_DEFAULT;

  if (cols_env && lines_env) {
    char *endptr_width = NULL, *endptr_height = NULL;
    int env_width = strtol(cols_env, &endptr_width, 10);
    int env_height = strtol(lines_env, &endptr_height, 10);

    if (endptr_width != cols_env && endptr_height != lines_env && env_width > 0 && env_height > 0) {
      *width = (unsigned short int)env_width;
      *height = (unsigned short int)env_height;
      return ASCIICHAT_OK;
    }
  }

  // Don't return error - just use default size
  return ASCIICHAT_OK;
}

/**
 * Get current TTY on Windows - just use CON device
 */
tty_info_t get_current_tty(void) {
  tty_info_t result = {-1, NULL, false};

  // On Windows, use CON for console output
  result.fd = platform_open("CON", _O_WRONLY, 0);
  if (result.fd >= 0) {
    result.path = "CON";
    result.owns_fd = true;
    log_debug("Windows TTY: CON (fd=%d)", result.fd);
    return result;
  }

  log_debug("Failed to open CON device: %s", SAFE_STRERROR(errno));
  return result; // No TTY available
}

/**
 * Validate TTY path on Windows - only CON is valid
 */
bool is_valid_tty_path(const char *path) {
  if (!path || strlen(path) == 0) {
    return false;
  }

  // Only CON is valid on Windows
  return (_stricmp(path, "CON") == 0);
}

/**
 * Detect color support on Windows - check ANSI support
 */
static terminal_color_level_t detect_windows_color_support(void) {
  // Check if we can enable ANSI processing
  HANDLE console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (console_handle == INVALID_HANDLE_VALUE) {
    // Don't log here - avoid color changes during detection
    return TERM_COLOR_NONE;
  }

  DWORD console_mode = 0;
  if (!GetConsoleMode(console_handle, &console_mode)) {
    // Don't log here - avoid color changes during detection
    // Even if we can't get console mode, modern Windows terminals support ANSI colors
    // Check for Windows Terminal or other modern terminals
    const char *wt_session = SAFE_GETENV("WT_SESSION");
    const char *conemu = SAFE_GETENV("ConEmuPID");
    if (wt_session || conemu) {
      // Don't log here - avoid color changes during detection
      return TERM_COLOR_TRUECOLOR;
    }
    return TERM_COLOR_NONE;
  }

  // Try to enable ANSI escape sequence processing
  DWORD new_mode = console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  if (SetConsoleMode(console_handle, new_mode)) {
    // Success! Windows supports ANSI escapes, assume truecolor
    // Don't log here - avoid color changes during detection
    return TERM_COLOR_TRUECOLOR;
  }

  // Even if SetConsoleMode fails, modern Windows terminals still support ANSI colors
  // Check for Windows Terminal or other modern terminals
  const char *wt_session = SAFE_GETENV("WT_SESSION");
  const char *conemu = SAFE_GETENV("ConEmuPID");
  if (wt_session || conemu) {
    // Don't log here - avoid color changes during detection
    return TERM_COLOR_TRUECOLOR;
  }

  // For regular Windows console, try to detect if it's Windows 10+ (which supports ANSI)
  // This is a fallback for cases where console mode detection fails
  // Use modern Windows version detection via RtlGetVersion
  typedef LONG(WINAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
  HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
  if (hMod) {
    RtlGetVersionPtr fxPtr = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
    if (fxPtr != NULL) {
      RTL_OSVERSIONINFOW osInfo = {0};
      osInfo.dwOSVersionInfoSize = sizeof(osInfo);
      if (fxPtr(&osInfo) == 0) {
        if (osInfo.dwMajorVersion >= 10) {
          // Don't log here - avoid color changes during detection
          return TERM_COLOR_TRUECOLOR;
        }
        // Don't log here - avoid color changes during detection
        return TERM_COLOR_16; // Fallback to 16-color for older Windows
      }
    }
  }

  // Final fallback: assume modern Windows with ANSI support
  // Don't log here - avoid color changes during detection
  return TERM_COLOR_TRUECOLOR;
}

/**
 * Detect UTF-8 support on Windows - check console code page
 */
static bool detect_windows_utf8_support(void) {
  UINT codepage = GetConsoleOutputCP();
  // Don't log here - avoid color changes during detection
  return (codepage == CP_UTF8);
}

/**
 * Detect terminal capabilities on Windows - much simpler than Unix!
 */
terminal_capabilities_t detect_terminal_capabilities(void) {
  terminal_capabilities_t caps = {0};

  // Detect color support level
  caps.color_level = detect_windows_color_support();

  // Set capability flags based on color level
  switch (caps.color_level) {
  case TERM_COLOR_TRUECOLOR:
    caps.capabilities |= TERM_CAP_COLOR_TRUE;
    caps.capabilities |= TERM_CAP_COLOR_256;
    caps.capabilities |= TERM_CAP_COLOR_16;
    caps.color_count = 16777216;
    caps.detection_reliable = true;
    break;

  case TERM_COLOR_256:
    caps.capabilities |= TERM_CAP_COLOR_256;
    caps.capabilities |= TERM_CAP_COLOR_16;
    caps.color_count = 256;
    caps.detection_reliable = true;
    break;

  case TERM_COLOR_16:
    caps.capabilities |= TERM_CAP_COLOR_16;
    caps.color_count = 16;
    caps.detection_reliable = false;
    break;

  case TERM_COLOR_NONE:
    caps.color_count = 0;
    caps.detection_reliable = false;
    break;
  }

  // Detect UTF-8 support
  caps.utf8_support = detect_windows_utf8_support();
  if (caps.utf8_support) {
    caps.capabilities |= TERM_CAP_UTF8;
  }

  // Background color support (assume yes if any color support)
  if (caps.color_level > TERM_COLOR_NONE) {
    caps.capabilities |= TERM_CAP_BACKGROUND;
  }

  // Store environment variables for debugging
  const char *term = SAFE_GETENV("TERM");
  const char *colorterm = SAFE_GETENV("COLORTERM");

  SAFE_STRNCPY(caps.term_type, term ? term : "windows-console", sizeof(caps.term_type) - 1);
  SAFE_STRNCPY(caps.colorterm, colorterm ? colorterm : "", sizeof(caps.colorterm) - 1);

  // Set default FPS for Windows terminals
  extern int g_max_fps;
  if (g_max_fps > 0) {
    caps.desired_fps = (uint8_t)(g_max_fps > 144 ? 144 : g_max_fps);
  } else {
    caps.desired_fps = DEFAULT_MAX_FPS; // 60 FPS on Windows with timeBeginPeriod(1)
  }

  // Don't log here - log after colors are initialized to avoid color changes
  // The log will be done in log_redetect_terminal_capabilities() after detection

  return caps;
}

/**
 * Helper functions for capability reporting
 */
const char *terminal_color_level_name(terminal_color_level_t level) {
  switch (level) {
  case TERM_COLOR_NONE:
    return "monochrome";
  case TERM_COLOR_16:
    return "16-color";
  case TERM_COLOR_256:
    return "256-color";
  case TERM_COLOR_TRUECOLOR:
    return "truecolor";
  default:
    return "unknown";
  }
}

const char *terminal_capabilities_summary(const terminal_capabilities_t *caps) {
  static char summary[256];

  safe_snprintf(summary, sizeof(summary), "%s (%d colors), UTF-8: %s, TERM: %s, COLORTERM: %s",
                terminal_color_level_name(caps->color_level), caps->color_count,
                (caps->capabilities & TERM_CAP_UTF8) ? "yes" : "no", caps->term_type, caps->colorterm);

  return summary;
}

void print_terminal_capabilities(const terminal_capabilities_t *caps) {
  printf("Terminal Capabilities (Windows):\n");
  printf("  Color Level: %s\n", terminal_color_level_name(caps->color_level));
  printf("  Max Colors: %d\n", caps->color_count);
  printf("  UTF-8 Support: %s\n", caps->utf8_support ? "Yes" : "No");
  printf("  Background Colors: %s\n", (caps->capabilities & TERM_CAP_BACKGROUND) ? "Yes" : "No");
  const char *render_mode_str;
  if (caps->render_mode == RENDER_MODE_FOREGROUND) {
    render_mode_str = "foreground";
  } else if (caps->render_mode == RENDER_MODE_BACKGROUND) {
    render_mode_str = "background";
  } else if (caps->render_mode == RENDER_MODE_HALF_BLOCK) {
    render_mode_str = "half-block";
  } else {
    render_mode_str = "unknown";
  }
  printf("  Render Mode: %s\n", render_mode_str);
  printf("  TERM: %s\n", caps->term_type);
  printf("  COLORTERM: %s\n", caps->colorterm);
  printf("  Detection Reliable: %s\n", caps->detection_reliable ? "Yes" : "No");
  printf("  Capabilities Bitmask: 0x%08x\n", caps->capabilities);
}

void test_terminal_output_modes(void) {
  printf("Testing Windows terminal output modes:\n");

  // Test basic ANSI colors (16-color)
  printf("  16-color: ");
  for (int i = 30; i <= 37; i++) {
    printf("\033[%dm█\033[0m", i);
  }
  printf("\n");

  // Test 256-color mode
  printf("  256-color: ");
  for (int i = 0; i < 16; i++) {
    printf("\033[38;5;%dm█\033[0m", i);
  }
  printf("\n");

  // Test truecolor mode
  printf("  Truecolor: ");
  for (int i = 0; i < 16; i++) {
    int r = (i * 255) / 15;
    printf("\033[38;2;%d;0;0m█\033[0m", r);
  }
  printf("\n");

  // Test Unicode characters
  printf("  Unicode: ");
  printf("░▒▓\n");
}

/**
 * Apply color mode and render mode overrides to detected capabilities
 */
terminal_capabilities_t apply_color_mode_override(terminal_capabilities_t caps) {
#ifndef NDEBUG
  // In debug builds, force no-color mode for Claude Code (LLM doesn't need colors, saves tokens)
  // Skip this for --show-capabilities since it's a diagnostic command
  if (!opt_show_capabilities && opt_color_mode == COLOR_MODE_AUTO && platform_getenv("CLAUDECODE")) {
    log_debug("CLAUDECODE detected: forcing no color mode");
    caps.color_level = TERM_COLOR_NONE;
    caps.capabilities &= ~((uint32_t)TERM_CAP_COLOR_16 | (uint32_t)TERM_CAP_COLOR_256 | (uint32_t)TERM_CAP_COLOR_TRUE);
    caps.color_count = 0;
    return caps;
  }
#endif

  // Handle color mode overrides
  switch (opt_color_mode) {
  case COLOR_MODE_AUTO:
    // Use detected capabilities as-is
    break;

  case COLOR_MODE_NONE:
    caps.color_level = TERM_COLOR_NONE;
    caps.color_count = 2;
    caps.capabilities &= ~((uint32_t)TERM_CAP_COLOR_TRUE | (uint32_t)TERM_CAP_COLOR_256 | (uint32_t)TERM_CAP_COLOR_16);
    break;

  case COLOR_MODE_16_COLOR:
    caps.color_level = TERM_COLOR_16;
    caps.color_count = 16;
    caps.capabilities &= ~((uint32_t)TERM_CAP_COLOR_TRUE | (uint32_t)TERM_CAP_COLOR_256);
    caps.capabilities |= (uint32_t)TERM_CAP_COLOR_16;
    break;

  case COLOR_MODE_256_COLOR:
    caps.color_level = TERM_COLOR_256;
    caps.color_count = 256;
    caps.capabilities &= ~(uint32_t)TERM_CAP_COLOR_TRUE;
    caps.capabilities |= ((uint32_t)TERM_CAP_COLOR_256 | (uint32_t)TERM_CAP_COLOR_16);
    break;

  case COLOR_MODE_TRUECOLOR:
    caps.color_level = TERM_COLOR_TRUECOLOR;
    caps.color_count = 16777216;
    caps.capabilities |= TERM_CAP_COLOR_TRUE | TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16;
    break;
  }

  // Handle render mode overrides
  switch (opt_render_mode) {
  case RENDER_MODE_FOREGROUND:
    caps.capabilities &= ~(uint32_t)TERM_CAP_BACKGROUND;
    break;

  case RENDER_MODE_BACKGROUND:
    caps.capabilities |= TERM_CAP_BACKGROUND;
    break;

  case RENDER_MODE_HALF_BLOCK:
    caps.capabilities |= TERM_CAP_UTF8 | TERM_CAP_BACKGROUND;
    break;
  }

  // Handle UTF-8 override
  if (opt_force_utf8) {
    caps.utf8_support = true;
    caps.capabilities |= TERM_CAP_UTF8;
  }

  // Include client's render mode preference
  caps.render_mode = opt_render_mode;

  return caps;
}

#endif // _WIN32
