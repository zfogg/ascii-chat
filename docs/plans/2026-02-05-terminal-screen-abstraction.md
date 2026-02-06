# Terminal Screen Abstraction Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Create a reusable "fixed header + scrolling logs" abstraction in the core module and refactor splash/status screens to use it

**Architecture:** Extract the common pattern of "fixed header + scrolling logs" (used by both splash.c and server_status.c) into a callback-based abstraction. Add terminal_screen to the CORE module since it's a core UI utility.

**Tech Stack:** C, CMake, session_log_buffer, display_width() for ANSI handling

---

## Task 1: Add terminal_screen to core module

**Files:**
- Modify: `cmake/targets/SourceFiles.cmake` (add terminal_screen.c to CORE_SRCS)

**Step 1: Add terminal_screen.c to CMake SourceFiles**

Modify: `cmake/targets/SourceFiles.cmake`

Find `CORE_SRCS` and add terminal_screen.c:

```cmake
set(CORE_SRCS
    lib/common.c
    lib/asciichat_errno.c
    # ... existing core sources ...
    lib/core/terminal_screen.c    # NEW: Terminal screen rendering abstraction
)
```

**Step 2: Verify CMake configuration**

Run: `cmake --preset default -B build`
Expected: Configuration succeeds

**Step 3: Commit**

```bash
git add cmake/targets/SourceFiles.cmake
git commit -m "build(core): Add terminal_screen.c to core module"
```

---

## Task 2: Create terminal_screen abstraction header

**Files:**
- Create: `include/ascii-chat/core/terminal_screen.h`

**Step 1: Write terminal_screen.h header**

Create: `include/ascii-chat/core/terminal_screen.h`

```c
/**
 * @file core/terminal_screen.h
 * @brief Reusable "fixed header + scrolling logs" terminal screen abstraction
 *
 * Provides a common pattern for rendering terminal screens with:
 * - Fixed header area (caller-defined via callback)
 * - Scrolling log feed below header (automatically managed)
 * - Terminal size caching to avoid error log spam
 * - ANSI-aware line wrapping using display_width()
 * - Latest log at bottom (standard terminal behavior)
 */

#pragma once

#include <ascii-chat/platform/terminal.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Callback to render the fixed header portion of the screen
 *
 * @param term_size Current terminal dimensions (cached, refreshed every 1 second)
 * @param user_data Caller-provided context data
 *
 * The callback should:
 * - Print exactly the number of lines specified in terminal_screen_config_t.fixed_header_lines
 * - Use display_width() to ensure lines don't exceed term_size.cols
 * - NOT clear the screen (terminal_screen_render does that)
 * - NOT print the final newline if it would be line N+1 (causes scroll)
 */
typedef void (*terminal_screen_header_fn)(terminal_size_t term_size, void *user_data);

/**
 * @brief Configuration for terminal screen rendering
 */
typedef struct {
  int fixed_header_lines;                    ///< How many lines the header takes (e.g., 4 for status, 8 for splash)
  terminal_screen_header_fn render_header;   ///< Callback to draw header content
  void *user_data;                           ///< Passed to render_header callback
  bool show_logs;                            ///< Whether to show log feed below header
} terminal_screen_config_t;

/**
 * @brief Render a terminal screen with fixed header and scrolling logs
 *
 * Renders a screen following the pattern:
 * 1. Clear screen and move cursor to home (both stdout and stderr)
 * 2. Call render_header() callback to draw fixed header
 * 3. Calculate log area: rows - fixed_header_lines - 1 (prevent scroll)
 * 4. Fetch recent logs from session_log_buffer
 * 5. Calculate which logs fit (working backwards, accounting for wrapping)
 * 6. Display logs chronologically (oldest to newest, latest at bottom)
 * 7. Fill remaining lines to prevent terminal scroll
 * 8. Flush stdout
 *
 * @param config Screen configuration (header callback, line count, etc.)
 *
 * Terminal size is cached internally with 1-second refresh interval to avoid
 * flooding error logs if terminal size checks fail repeatedly.
 */
void terminal_screen_render(const terminal_screen_config_t *config);
```

**Step 2: Commit**

```bash
git add include/ascii-chat/core/terminal_screen.h
git commit -m "feat(core): Add terminal_screen.h abstraction for fixed header + scrolling logs"
```

---

## Task 3: Implement terminal_screen.c core rendering

**Files:**
- Create: `lib/core/terminal_screen.c`

**Step 1: Write terminal_screen.c implementation**

Create: `lib/core/terminal_screen.c`

```c
/**
 * @file core/terminal_screen.c
 * @brief Reusable "fixed header + scrolling logs" terminal screen abstraction
 */

#include <ascii-chat/core/terminal_screen.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdio.h>
#include <string.h>

// Cached terminal size (to avoid flooding logs with terminal_get_size errors)
static terminal_size_t g_cached_term_size = {.rows = 24, .cols = 80};
static uint64_t g_last_term_size_check_us = 0;
#define TERM_SIZE_CHECK_INTERVAL_US 1000000ULL // Check terminal size max once per second

void terminal_screen_render(const terminal_screen_config_t *config) {
  if (!config || !config->render_header) {
    SET_ERRNO(ERROR_INVALID_PARAM, "invalid param")
    return;
  }

  // Get terminal dimensions (cached to avoid flooding logs with errors)
  uint64_t now_us = platform_get_monotonic_time_us();
  if (now_us - g_last_term_size_check_us > TERM_SIZE_CHECK_INTERVAL_US) {
    terminal_size_t temp_size;
    if (terminal_get_size(&temp_size) == ASCIICHAT_OK) {
      g_cached_term_size = temp_size;
    }
    g_last_term_size_check_us = now_us;
  }
  terminal_size_t term_size = g_cached_term_size;

  // Clear screen and move to home position on BOTH stdout and stderr
  // (logs may have been written to either stream before screen started)
  fprintf(stderr, "\033[H\033[2J");
  fflush(stderr);
  printf("\033[H\033[2J");

  // ========================================================================
  // FIXED HEADER - ICaller renders via callback
  // ========================================================================
  config->render_header(term_size, config->user_data);

  // ========================================================================
  // LIVE LOG FEED - Fills remaining screen (never causes scroll)
  // ========================================================================
  if (!config->show_logs) {
    // No logs requested, just flush and return
    fflush(stdout);
    return;
  }

  // Calculate EXACTLY how many lines we have for logs
  // Header took exactly config->fixed_header_lines, reserve 1 line to prevent cursor scroll
  int logs_available_lines = term_size.rows - config->fixed_header_lines - 1;
  if (logs_available_lines < 1) {
    logs_available_lines = 1; // At least show something
  }

  // Get recent log entries
  session_log_entry_t logs[SESSION_LOG_BUFFER_SIZE];
  size_t log_count = session_log_buffer_get_recent(logs, SESSION_LOG_BUFFER_SIZE);

  // Calculate actual display lines needed for each log (accounting for wrapping and newlines)
  // Work backwards from most recent logs until we fill available lines
  int lines_used = 0;
  size_t start_idx = log_count; // Start past the end, will work backwards

  for (size_t i = log_count; i > 0; i--) {
    size_t idx = i - 1;
    const char *msg = logs[idx].message;

    // Count display lines for this message (newlines + wrapping)
    // Split message by newlines and calculate visible width of each line
    int msg_lines = 0;
    const char *line_start = msg;
    const char *p = msg;

    while (*p) {
      if (*p == '\n') {
        // Calculate visible width of this line (excluding ANSI codes)
        size_t line_len = p - line_start;
        char line_buf[2048];
        if (line_len < sizeof(line_buf)) {
          memcpy(line_buf, line_start, line_len);
          line_buf[line_len] = '\0';

          int visible_width = display_width(line_buf);
          if (visible_width < 0)
            visible_width = (int)line_len; // Fallback

          // Calculate how many terminal lines this takes (with wrapping)
          if (term_size.cols > 0 && visible_width > 0) {
            msg_lines += (visible_width + term_size.cols - 1) / term_size.cols;
          } else {
            msg_lines += 1;
          }
        } else {
          msg_lines += 1; // Line too long, just count as 1
        }

        line_start = p + 1;
      }
      p++;
    }

    // Handle final line if message doesn't end with newline
    if (line_start < p) {
      size_t line_len = p - line_start;
      char line_buf[2048];
      if (line_len < sizeof(line_buf)) {
        memcpy(line_buf, line_start, line_len);
        line_buf[line_len] = '\0';

        int visible_width = display_width(line_buf);
        if (visible_width < 0)
          visible_width = (int)line_len; // Fallback

        // Calculate how many terminal lines this takes (with wrapping)
        if (term_size.cols > 0 && visible_width > 0) {
          msg_lines += (visible_width + term_size.cols - 1) / term_size.cols;
        } else {
          msg_lines += 1;
        }
      } else {
        msg_lines += 1;
      }
    }

    // Check if this log fits in remaining space
    if (lines_used + msg_lines <= logs_available_lines) {
      lines_used += msg_lines;
      start_idx = idx;
    } else {
      break; // No more room
    }
  }

  // Display logs that fit (starting from start_idx)
  // Logs are already formatted with colors from logging.c
  for (size_t i = start_idx; i < log_count; i++) {
    printf("%s", logs[i].message);
    if (logs[i].message[0] != '\0' && logs[i].message[strlen(logs[i].message) - 1] != '\n') {
      printf("\n");
    }
  }

  // Fill remaining lines to reach EXACTLY the bottom of screen without scrolling
  for (int i = lines_used; i < logs_available_lines; i++) {
    printf("\n");
  }

  fflush(stdout);
}
```

**Step 2: Build and verify compilation**

Run: `cmake --build build --parallel 8`
Expected: Successful compilation

**Step 3: Commit**

```bash
git add lib/core/terminal_screen.c
git commit -m "feat(core): Implement terminal_screen.c rendering engine"
```

---

## Task 4: Refactor server_status.c to use terminal_screen

**Files:**
- Modify: `lib/session/server_status.c`
- Modify: `include/ascii-chat/session/server_status.h` (if needed)

**Step 1: Update server_status.c includes and add header callback**

Modify: `lib/session/server_status.c`

Add include at top:
```c
#include <ascii-chat/core/terminal_screen.h>
```

Remove these now-unused lines (around line 20-23):
```c
// Cached terminal size (to avoid flooding logs with terminal_get_size errors)
static terminal_size_t g_cached_term_size = {.rows = 24, .cols = 80};
static uint64_t g_last_term_size_check_us = 0;
#define TERM_SIZE_CHECK_INTERVAL_US 1000000ULL // Check terminal size max once per second
```

**Step 2: Create header rendering callback**

Add before `server_status_display()`:

```c
/**
 * @brief Render the server status header (4 fixed lines)
 * Callback for terminal_screen_render()
 */
static void render_server_status_header(terminal_size_t term_size, void *user_data) {
  const server_status_t *status = (const server_status_t *)user_data;
  if (!status) {
    SET_ERRNO(ERROR_INVALID_PARAM, "invalid param")
    return;
  }

  // Calculate uptime
  time_t now = time(NULL);
  time_t uptime_secs = now - status->start_time;
  int uptime_hours = uptime_secs / 3600;
  int uptime_mins = (uptime_secs % 3600) / 60;
  int uptime_secs_rem = uptime_secs % 60;

  // Line 1: Top border
  printf("\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    printf("━");
  }
  printf("\033[0m\n");

  // Line 2: Title + Stats (truncate if too long to fit on one line)
  char status_line[512];
  snprintf(status_line, sizeof(status_line), "  ascii-chat %s | 👥 %zu | ⏱️ %dh %dm %ds", status->mode_name,
           status->connected_count, uptime_hours, uptime_mins, uptime_secs_rem);
  if (term_size.cols > 0 && (int)strlen(status_line) >= term_size.cols) {
    status_line[term_size.cols - 1] = '\0'; // Truncate
  }
  printf("\033[1;36m%s\033[0m\n", status_line);

  // Line 3: Session + Addresses (truncate if too long)
  char addr_line[512];
  int pos = 0;
  if (status->session_string[0] != '\0') {
    pos += snprintf(addr_line + pos, sizeof(addr_line) - pos, "  🔗 %s", status->session_string);
  }
  if (status->ipv4_bound && pos < (int)sizeof(addr_line) - 30) {
    pos += snprintf(addr_line + pos, sizeof(addr_line) - pos, " | %s", status->ipv4_address);
  }
  if (status->ipv6_bound && pos < (int)sizeof(addr_line) - 30) {
    snprintf(addr_line + pos, sizeof(addr_line) - pos, " | %s", status->ipv6_address);
  }
  if (term_size.cols > 0 && (int)strlen(addr_line) >= term_size.cols) {
    addr_line[term_size.cols - 1] = '\0'; // Truncate
  }
  printf("%s\n", addr_line);

  // Line 4: Bottom border
  printf("\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    printf("━");
  }
  printf("\033[0m\n");
}
```

**Step 3: Simplify server_status_display() to use abstraction**

Replace the entire `server_status_display()` function (lines 89-265) with:

```c
void server_status_display(const server_status_t *status) {
  if (!status) {
    SET_ERRNO(ERROR_INVALID_PARAM, "invalid param")
    return;
  }

  terminal_screen_config_t config = {
      .fixed_header_lines = 4,
      .render_header = render_server_status_header,
      .user_data = (void *)status,
      .show_logs = true,
  };

  terminal_screen_render(&config);
}
```

**Step 4: Remove old log rendering code**

Delete the static function `server_status_log_get_recent()` (around line 45) since terminal_screen.c calls session_log_buffer_get_recent() directly.

**Step 5: Build and test**

Run: `cmake --build build --parallel 8`
Expected: Successful build

Run server to verify status screen still works:
```bash
./build/bin/ascii-chat server
```
Expected: Status screen displays with logs scrolling below

**Step 6: Commit**

```bash
git add lib/session/server_status.c
git commit -m "refactor(session): Refactor server_status to use terminal_screen abstraction

Removes ~150 lines of duplicate log rendering code."
```

---

## Task 5: Refactor splash.c to use terminal_screen

**Files:**
- Modify: `lib/session/splash.c`

**Step 1: Update splash.c includes**

Modify: `lib/session/splash.c`

Add include at top:
```c
#include <ascii-chat/core/terminal_screen.h>
```

Remove these now-unused lines (around line 215-218):
```c
// Cached terminal size (to avoid flooding logs with terminal_get_size errors)
static terminal_size_t g_splash_cached_term_size = {.rows = 24, .cols = 80};
static uint64_t g_splash_last_term_size_check_us = 0;
#define SPLASH_TERM_SIZE_CHECK_INTERVAL_US 1000000ULL // Check terminal size max once per second
```

**Step 2: Create splash header rendering callback with rainbow animation**

Add new struct for splash rendering state:

```c
/**
 * @brief User data for splash header rendering
 */
typedef struct {
  int frame;           // Animation frame counter
  bool use_colors;     // Whether terminal supports colors
} splash_render_data_t;
```

Add before `splash_animation_thread()`:

```c
/**
 * @brief Render the splash screen header (8 fixed lines with rainbow animation)
 * Callback for terminal_screen_render()
 */
static void render_splash_header(terminal_size_t term_size, void *user_data) {
  splash_render_data_t *data = (splash_render_data_t *)user_data;
  if (!data) {
    SET_ERRNO(ERROR_INVALID_PARAM, "invalid param")
    return;
  }

  // ASCII logo lines (same as help output)
  const char *ascii_logo[4] = {
      "  __ _ ___  ___(_|_)       ___| |__   __ _| |_ ",
      " / _` / __|/ __| | |_____ / __| '_ \\ / _` | __| ",
      "| (_| \\__ \\ (__| | |_____| (__| | | | (_| | |_ ",
      " \\__,_|___/\\___|_|_|      \\___|_| |_|\\__,_|\\__| "
  };
  const char *tagline = "Video chat in your terminal";
  const int logo_width = 52;

  // Calculate rainbow offset for this frame (smooth continuous wave)
  const double rainbow_speed = 0.01; // Characters per frame of wave speed
  double offset = data->frame * rainbow_speed;

  // Line 1: Top border
  printf("\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    printf("━");
  }
  printf("\033[0m\n");

  // Lines 2-5: ASCII logo (centered, truncated if too long)
  for (int logo_line = 0; logo_line < 4; logo_line++) {
    // Build plain text line first (for width calculation)
    char plain_line[512];
    int horiz_pad = (term_size.cols - logo_width) / 2;
    if (horiz_pad < 0) {
      horiz_pad = 0;
    }

    int pos = 0;
    for (int j = 0; j < horiz_pad && pos < (int)sizeof(plain_line) - 1; j++) {
      plain_line[pos++] = ' ';
    }
    snprintf(plain_line + pos, sizeof(plain_line) - pos, "%s", ascii_logo[logo_line]);

    // Check visible width and truncate if needed
    int visible_width = display_width(plain_line);
    if (visible_width < 0) {
      visible_width = (int)strlen(plain_line);
    }
    if (term_size.cols > 0 && visible_width >= term_size.cols) {
      plain_line[term_size.cols - 1] = '\0';
    }

    // Print with rainbow colors
    int char_idx = 0;
    for (int i = 0; plain_line[i] != '\0'; i++) {
      char ch = plain_line[i];
      if (ch == ' ') {
        printf(" ");
      } else if (data->use_colors) {
        double char_pos = (data->frame * 52 + char_idx + offset) / 30.0;
        rgb_color_t color = get_rainbow_color_rgb(char_pos);
        printf("\x1b[38;2;%u;%u;%um%c\x1b[0m", color.r, color.g, color.b, ch);
        char_idx++;
      } else {
        printf("%c", ch);
        char_idx++;
      }
    }
    printf("\n");
  }

  // Line 6: Blank line
  printf("\n");

  // Line 7: Tagline (centered, truncated if too long)
  char plain_tagline[512];
  int tagline_len = (int)strlen(tagline);
  int tagline_pad = (term_size.cols - tagline_len) / 2;
  if (tagline_pad < 0) {
    tagline_pad = 0;
  }

  int tpos = 0;
  for (int j = 0; j < tagline_pad && tpos < (int)sizeof(plain_tagline) - 1; j++) {
    plain_tagline[tpos++] = ' ';
  }
  snprintf(plain_tagline + tpos, sizeof(plain_tagline) - tpos, "%s", tagline);

  // Check visible width and truncate if needed
  int tagline_visible_width = display_width(plain_tagline);
  if (tagline_visible_width < 0) {
    tagline_visible_width = (int)strlen(plain_tagline);
  }
  if (term_size.cols > 0 && tagline_visible_width >= term_size.cols) {
    plain_tagline[term_size.cols - 1] = '\0';
  }

  printf("%s\n", plain_tagline);

  // Line 8: Bottom border
  printf("\033[1;36m━");
  for (int i = 1; i < term_size.cols - 1; i++) {
    printf("━");
  }
  printf("\033[0m\n");
}
```

**Step 3: Simplify splash_animation_thread() to use abstraction**

Replace the rendering loop in `splash_animation_thread()` (lines 242-460) with:

```c
static void *splash_animation_thread(void *arg) {
  (void)arg;

  // Check if colors should be used (TTY check)
  bool use_colors = terminal_should_color_output(STDOUT_FILENO);

  // Animate with rainbow wave effect
  int frame = 0;
  const int anim_speed = 100; // milliseconds per frame

  while (!atomic_load(&g_splash_state.should_stop)) {
    // Prepare rendering data
    splash_render_data_t render_data = {
        .frame = frame,
        .use_colors = use_colors,
    };

    // Configure terminal screen
    terminal_screen_config_t config = {
        .fixed_header_lines = 8,
        .render_header = render_splash_header,
        .user_data = &render_data,
        .show_logs = true,
    };

    // Render the screen
    terminal_screen_render(&config);

    // Sleep between frames
    platform_sleep_ms(anim_speed);
    frame++;
  }

  return NULL;
}
```

**Step 4: Build and test**

Run: `cmake --build build --parallel 8`
Expected: Successful build

Run client to verify splash screen:
```bash
./build/bin/ascii-chat client
```
Expected: Splash screen displays with rainbow animation and logs scrolling below

**Step 5: Commit**

```bash
git add lib/session/splash.c
git commit -m "refactor(session): Refactor splash to use terminal_screen abstraction

Removes ~200 lines of duplicate log rendering code."
```

---

## Task 6: Verify final build and test

**Files:**
- Verify: All files compile cleanly
- Test: Run server, client modes

**Step 1: Clean and rebuild**

```bash
rm -rf build
cmake --preset default -B build
cmake --build build --parallel 8
```

Expected: Clean build with no errors

**Step 2: Test server status screen**

```bash
./build/bin/ascii-chat server
```

Expected:
- Status screen displays with 4-line header
- Logs scroll below header
- No flashing or artifacts
- Latest logs appear at bottom

**Step 3: Test client splash screen**

```bash
./build/bin/ascii-chat client
```

Expected:
- Splash displays with rainbow animation
- Logs scroll below splash
- No flashing or artifacts
- Splash stops when connection established

**Step 4: Commit if any fixes were needed**

```bash
git add <any-fixed-files>
git commit -m "fix(core): Final fixes after terminal_screen refactoring"
```

---

## Summary

This plan:
1. Adds `terminal_screen` abstraction to the CORE module
2. Implements callback-based "fixed header + scrolling logs" pattern
3. Refactors `server_status.c` to use abstraction (removes ~150 lines)
4. Refactors `splash.c` to use abstraction (removes ~200 lines)
5. Maintains exact behavior: 4 lines for status, 8 lines for splash, logs at bottom

**Total LOC reduction:** ~350 lines of duplicate log rendering code eliminated
**Architecture improvement:** Single source of truth for terminal screen rendering
**Maintainability:** Core utility function available to all session/UI code
