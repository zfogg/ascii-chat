# Web Mirror WASM Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build full ascii-chat library as WASM with interactive settings panel for web mirror mode

**Architecture:** WASM-specific platform layer (`lib/platform/wasm/`) implementing the platform abstraction API, compiled with Emscripten, exposed to TypeScript via exported functions, with React settings UI

**Tech Stack:** Emscripten, C11, TypeScript, React, xterm.js, Tailwind CSS

---

## Task 1: Platform Layer - Terminal Bridge

**Files:**
- Create: `lib/platform/wasm/terminal.c`
- Reference: `include/ascii-chat/platform/abstraction.h`
- Reference: `include/ascii-chat/platform/terminal.h`

**Step 1: Create platform directory**

```bash
mkdir -p lib/platform/wasm
```

**Step 2: Create terminal.c with EM_JS bridge**

Create `lib/platform/wasm/terminal.c`:

```c
/**
 * @file platform/wasm/terminal.c
 * @brief Terminal abstraction for WASM/Emscripten via EM_JS bridge to xterm.js
 */

#include <emscripten.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// EM_JS: JavaScript bridge functions
// ============================================================================

EM_JS(int, js_get_terminal_cols, (), {
    return Module.xterm ? Module.xterm.cols : 80;
});

EM_JS(int, js_get_terminal_rows, (), {
    return Module.xterm ? Module.xterm.rows : 24;
});

EM_JS(int, js_get_color_mode, (), {
    if (!Module.xterm) return 0; // COLOR_MODE_AUTO
    return 4; // COLOR_MODE_TRUECOLOR (xterm.js supports it)
});

EM_JS(void, js_terminal_write, (const char* data, int len), {
    if (Module.xterm) {
        Module.xterm.write(UTF8ToString(data, len));
    }
});

// ============================================================================
// Platform API Implementation
// ============================================================================

int platform_get_terminal_size(int *cols, int *rows) {
    if (!cols || !rows) {
        return -1;
    }
    *cols = js_get_terminal_cols();
    *rows = js_get_terminal_rows();
    return 0;
}

color_mode_t platform_detect_color_support(void) {
    return (color_mode_t)js_get_color_mode();
}

int platform_write_terminal(const char *data, size_t len) {
    js_terminal_write(data, (int)len);
    return (int)len;
}

// Stub implementations (not needed for mirror mode)
int platform_set_terminal_raw_mode(bool enable) {
    (void)enable;
    return 0; // No-op: xterm.js handles this
}

int platform_read_keyboard(char *buffer, size_t len) {
    (void)buffer;
    (void)len;
    return -1; // Not supported: use JavaScript event listeners
}

bool platform_is_terminal(int fd) {
    (void)fd;
    return true; // Always true for WASM terminal
}

int platform_get_cursor_position(int *row, int *col) {
    (void)row;
    (void)col;
    return -1; // Not supported in WASM
}

int platform_set_cursor_position(int row, int col) {
    (void)row;
    (void)col;
    return -1; // Not supported in WASM
}
```

**Step 3: Verify file created**

Run: `ls -la lib/platform/wasm/terminal.c`
Expected: File exists, ~1.5KB

**Step 4: Commit**

```bash
git add lib/platform/wasm/terminal.c
git commit -m "feat(wasm): Add terminal platform abstraction with EM_JS bridge

Implement platform_get_terminal_size, platform_detect_color_support,
and platform_write_terminal using EM_JS to bridge to xterm.js instance.

- EM_JS functions call Module.xterm from JavaScript
- Stub implementations for unsupported functions
- Provides terminal capabilities for WASM mirror mode

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 2: Platform Layer - Threading

**Files:**
- Create: `lib/platform/wasm/threading.c`

**Step 1: Create threading.c**

Create `lib/platform/wasm/threading.c`:

```c
/**
 * @file platform/wasm/threading.c
 * @brief Threading abstraction for WASM/Emscripten using pthreads
 */

#include <ascii-chat/platform/abstraction.h>
#include <pthread.h>

// Emscripten provides pthread.h
// In single-threaded mode (USE_PTHREADS=0), these are no-ops
// In multi-threaded mode (USE_PTHREADS=1), these are real mutexes

int mutex_init(mutex_t *mutex) {
    if (!mutex) return -1;
    return pthread_mutex_init(mutex, NULL);
}

int mutex_lock(mutex_t *mutex) {
    if (!mutex) return -1;
    return pthread_mutex_lock(mutex);
}

int mutex_unlock(mutex_t *mutex) {
    if (!mutex) return -1;
    return pthread_mutex_unlock(mutex);
}

int mutex_destroy(mutex_t *mutex) {
    if (!mutex) return -1;
    return pthread_mutex_destroy(mutex);
}

int mutex_trylock(mutex_t *mutex) {
    if (!mutex) return -1;
    return pthread_mutex_trylock(mutex);
}

// Thread creation (not used in mirror mode, but provided for completeness)
int asciichat_thread_create(asciichat_thread_t *thread, void *(*start_routine)(void *), void *arg) {
    if (!thread || !start_routine) return -1;
    return pthread_create(thread, NULL, start_routine, arg);
}

int asciichat_thread_join(asciichat_thread_t thread, void **retval) {
    return pthread_join(thread, retval);
}

int asciichat_thread_detach(asciichat_thread_t thread) {
    return pthread_detach(thread);
}

asciichat_thread_t asciichat_thread_self(void) {
    return pthread_self();
}

int asciichat_thread_equal(asciichat_thread_t t1, asciichat_thread_t t2) {
    return pthread_equal(t1, t2);
}
```

**Step 2: Verify file created**

Run: `ls -la lib/platform/wasm/threading.c`
Expected: File exists, ~1.2KB

**Step 3: Commit**

```bash
git add lib/platform/wasm/threading.c
git commit -m "feat(wasm): Add threading platform abstraction

Implement mutex and thread functions using Emscripten pthreads.
In single-threaded mode (USE_PTHREADS=0), mutexes are no-ops.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 3: Platform Layer - Environment and Time

**Files:**
- Create: `lib/platform/wasm/environment.c`
- Create: `lib/platform/wasm/time.c`

**Step 1: Create environment.c**

Create `lib/platform/wasm/environment.c`:

```c
/**
 * @file platform/wasm/environment.c
 * @brief Environment and string utilities for WASM/Emscripten
 */

#include <ascii-chat/platform/abstraction.h>
#include <stdlib.h>
#include <string.h>

const char* platform_getenv(const char *name) {
    if (!name) return NULL;
    return getenv(name);
}

const char* platform_strerror(int errnum) {
    return strerror(errnum);
}

int platform_setenv(const char *name, const char *value, int overwrite) {
    (void)name;
    (void)value;
    (void)overwrite;
    return -1; // Not supported in WASM
}

int platform_unsetenv(const char *name) {
    (void)name;
    return -1; // Not supported in WASM
}
```

**Step 2: Create time.c**

Create `lib/platform/wasm/time.c`:

```c
/**
 * @file platform/wasm/time.c
 * @brief Time functions for WASM/Emscripten
 */

#include <ascii-chat/platform/abstraction.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>

uint64_t platform_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

uint64_t platform_get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

void platform_sleep_ms(uint64_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

void platform_sleep_us(uint64_t us) {
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;
    nanosleep(&ts, NULL);
}
```

**Step 3: Verify files created**

Run: `ls -la lib/platform/wasm/{environment,time}.c`
Expected: Both files exist

**Step 4: Commit**

```bash
git add lib/platform/wasm/environment.c lib/platform/wasm/time.c
git commit -m "feat(wasm): Add environment and time platform abstractions

- environment.c: getenv, strerror (setenv/unsetenv not supported)
- time.c: platform_get_time_ms/us, platform_sleep_ms/us

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 4: Platform Layer - Initialization

**Files:**
- Create: `lib/platform/wasm/init.c`

**Step 1: Create init.c**

Create `lib/platform/wasm/init.c`:

```c
/**
 * @file platform/wasm/init.c
 * @brief Platform initialization for WASM/Emscripten
 */

#include <ascii-chat/platform/init.h>
#include <ascii-chat/asciichat_errno.h>

asciichat_error_t platform_init(void) {
    // No special initialization needed for WASM
    return ASCIICHAT_OK;
}

void platform_cleanup(void) {
    // No cleanup needed
}
```

**Step 2: Verify file created**

Run: `ls -la lib/platform/wasm/init.c`
Expected: File exists, ~350 bytes

**Step 3: Commit**

```bash
git add lib/platform/wasm/init.c
git commit -m "feat(wasm): Add platform initialization stubs

No special initialization or cleanup needed for WASM platform.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 5: WASM Entry Point

**Files:**
- Create: `web/web.ascii-chat.com/wasm/mirror_wasm.c`

**Step 1: Create wasm directory**

```bash
mkdir -p web/web.ascii-chat.com/wasm
```

**Step 2: Create mirror_wasm.c (initialization functions only)**

Create `web/web.ascii-chat.com/wasm/mirror_wasm.c`:

```c
/**
 * @file mirror_wasm.c
 * @brief WASM entry point for ascii-chat mirror mode
 */

#include <emscripten.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/asciichat_errno.h>

// ============================================================================
// Initialization
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_init(int width, int height) {
    // Initialize platform layer
    asciichat_error_t err = platform_init();
    if (err != ASCIICHAT_OK) {
        return -1;
    }

    // Create minimal argc/argv for options system
    char *argv[] = {"mirror", NULL};
    int argc = 1;

    // Initialize options (sets up RCU, defaults, etc.)
    err = options_init(argc, argv);
    if (err != ASCIICHAT_OK) {
        return -1;
    }

    // Override dimensions with actual values from xterm.js
    options_set_int("width", width);
    options_set_int("height", height);

    return 0;
}

EMSCRIPTEN_KEEPALIVE
void mirror_cleanup(void) {
    options_cleanup();
    platform_cleanup();
}

// ============================================================================
// Settings API - Dimension Getters/Setters
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_width(int width) {
    if (width <= 0 || width > 1000) {
        return -1;
    }
    asciichat_error_t err = options_set_int("width", width);
    return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_set_height(int height) {
    if (height <= 0 || height > 1000) {
        return -1;
    }
    asciichat_error_t err = options_set_int("height", height);
    return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_width(void) {
    return GET_OPTION(width);
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_height(void) {
    return GET_OPTION(height);
}

// Settings API continues in next task...
```

**Step 3: Verify file created**

Run: `ls -la web/web.ascii-chat.com/wasm/mirror_wasm.c`
Expected: File exists, ~1.5KB

**Step 4: Commit**

```bash
git add web/web.ascii-chat.com/wasm/mirror_wasm.c
git commit -m "feat(wasm): Add WASM entry point with init and dimension API

Implement mirror_init, mirror_cleanup, and dimension getters/setters.
Uses options_set_int for thread-safe updates via RCU.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 6: WASM Entry Point - Render Settings

**Files:**
- Modify: `web/web.ascii-chat.com/wasm/mirror_wasm.c`

**Step 1: Add render mode and color settings**

Append to `web/web.ascii-chat.com/wasm/mirror_wasm.c`:

```c
// ============================================================================
// Settings API - Render Mode
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_render_mode(int mode) {
    // mode: 0=foreground, 1=background, 2=half-block
    if (mode < 0 || mode > 2) {
        return -1;
    }
    asciichat_error_t err = options_set_int("render_mode", mode);
    return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_render_mode(void) {
    return GET_OPTION(render_mode);
}

// ============================================================================
// Settings API - Color Mode
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_color_mode(int mode) {
    // mode: 0=auto, 1=none, 2=16, 3=256, 4=truecolor
    if (mode < 0 || mode > 4) {
        return -1;
    }
    asciichat_error_t err = options_set_int("color_mode", mode);
    return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_color_mode(void) {
    return GET_OPTION(color_mode);
}

// ============================================================================
// Settings API - Color Filter
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_color_filter(int filter) {
    // filter: 0=none, 1=black, 2=white, 3=green, 4=magenta, etc.
    if (filter < 0 || filter > 11) {
        return -1;
    }
    asciichat_error_t err = options_set_int("color_filter", filter);
    return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_color_filter(void) {
    return GET_OPTION(color_filter);
}
```

**Step 2: Verify additions**

Run: `wc -l web/web.ascii-chat.com/wasm/mirror_wasm.c`
Expected: ~100-120 lines

**Step 3: Commit**

```bash
git add web/web.ascii-chat.com/wasm/mirror_wasm.c
git commit -m "feat(wasm): Add render mode, color mode, and color filter API

Implement getters/setters for:
- Render mode (foreground, background, half-block)
- Color mode (auto, none, 16, 256, truecolor)
- Color filter (none, green, cyan, magenta, etc.)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 7: WASM Entry Point - Frame Conversion

**Files:**
- Modify: `web/web.ascii-chat.com/wasm/mirror_wasm.c`

**Step 1: Add required includes at top of file**

Add after existing includes in `mirror_wasm.c`:

```c
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/color_filter.h>
#include <ascii-chat/video/framebuffer.h>
#include <ascii-chat/common.h>
```

**Step 2: Add frame conversion function**

Append to `web/web.ascii-chat.com/wasm/mirror_wasm.c`:

```c
// ============================================================================
// Frame Conversion API
// ============================================================================

EMSCRIPTEN_KEEPALIVE
char* mirror_convert_frame(
    uint8_t *rgba_data,
    int src_width,
    int src_height
) {
    if (!rgba_data || src_width <= 0 || src_height <= 0) {
        return NULL;
    }

    // Get current settings from options
    int dst_width = GET_OPTION(width);
    int dst_height = GET_OPTION(height);
    color_filter_t filter = (color_filter_t)GET_OPTION(color_filter);
    render_mode_t render_mode = (render_mode_t)GET_OPTION(render_mode);

    // Apply color filter to RGBA data if needed
    if (filter != COLOR_FILTER_NONE) {
        // Convert RGBA to RGB (color_filter expects RGB24)
        int stride = src_width * 3;
        uint8_t *rgb_data = SAFE_MALLOC(src_width * src_height * 3, uint8_t *);
        if (!rgb_data) {
            return NULL;
        }

        // Extract RGB from RGBA
        for (int i = 0; i < src_width * src_height; i++) {
            rgb_data[i * 3 + 0] = rgba_data[i * 4 + 0]; // R
            rgb_data[i * 3 + 1] = rgba_data[i * 4 + 1]; // G
            rgb_data[i * 3 + 2] = rgba_data[i * 4 + 2]; // B
        }

        // Apply filter (modifies rgb_data in-place)
        apply_color_filter(rgb_data, src_width, src_height, stride, filter);

        // Copy filtered RGB back to RGBA
        for (int i = 0; i < src_width * src_height; i++) {
            rgba_data[i * 4 + 0] = rgb_data[i * 3 + 0];
            rgba_data[i * 4 + 1] = rgb_data[i * 3 + 1];
            rgba_data[i * 4 + 2] = rgb_data[i * 3 + 2];
            // Alpha unchanged
        }

        SAFE_FREE(rgb_data);
    }

    // Create framebuffer for ASCII output
    framebuffer_t fb;
    if (framebuffer_init(&fb, dst_width, dst_height) != ASCIICHAT_OK) {
        return NULL;
    }

    // Convert to ASCII using full library rendering
    asciichat_error_t err = convert_image_to_ascii(
        rgba_data, src_width, src_height,
        &fb, render_mode
    );

    if (err != ASCIICHAT_OK) {
        framebuffer_cleanup(&fb);
        return NULL;
    }

    // Generate ANSI escape sequences from framebuffer
    char *output = generate_ansi_output(&fb, render_mode);

    framebuffer_cleanup(&fb);
    return output;
}

EMSCRIPTEN_KEEPALIVE
void mirror_free_string(char *ptr) {
    SAFE_FREE(ptr);
}
```

**Step 3: Verify file complete**

Run: `grep -c "EMSCRIPTEN_KEEPALIVE" web/web.ascii-chat.com/wasm/mirror_wasm.c`
Expected: 12 (12 exported functions)

**Step 4: Commit**

```bash
git add web/web.ascii-chat.com/wasm/mirror_wasm.c
git commit -m "feat(wasm): Add frame conversion API

Implement mirror_convert_frame using full library:
- Applies color filters via apply_color_filter()
- Converts to ASCII via convert_image_to_ascii()
- Generates ANSI output via generate_ansi_output()
- Uses current settings from options system

Also add mirror_free_string for memory cleanup.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 8: CMake Build Configuration

**Files:**
- Create: `web/web.ascii-chat.com/wasm/CMakeLists.txt`

**Step 1: Create CMakeLists.txt**

Create `web/web.ascii-chat.com/wasm/CMakeLists.txt`:

```cmake
# WebAssembly Mirror Mode Build
# Full ascii-chat library compiled to WASM

cmake_minimum_required(VERSION 3.18)
project(ascii-chat-mirror-wasm C)

# Verify we're using Emscripten
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This CMakeLists.txt requires Emscripten. Use: emcmake cmake ..")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================================
# Build Configuration
# ============================================================================

# Define WASM-specific flags
add_definitions(
    -DEMSCRIPTEN_BUILD=1
    -DWASM_MIRROR_MODE=1
    -DNO_NETWORK=1
    -DNO_CRYPTO=1
    -DNO_AUDIO=1
    -DNO_FFMPEG=1
    -DNO_WEBCAM_CAPTURE=1
    -DNO_PCRE2=1
    -DPCRE2_CODE_UNIT_WIDTH=8  # Keep defined for header compatibility
)

# ============================================================================
# Source Files
# ============================================================================

# Point to main repo root (3 levels up: wasm/ -> web.ascii-chat.com/ -> web/ -> repo/)
set(REPO_ROOT ${CMAKE_SOURCE_DIR}/../../..)

# Include directories
include_directories(
    ${REPO_ROOT}/include
    ${REPO_ROOT}/src
)

# Collect source files from main repo
set(WASM_SOURCES
    # Entry point
    ${CMAKE_SOURCE_DIR}/mirror_wasm.c

    # Platform layer (WASM-specific)
    ${REPO_ROOT}/lib/platform/wasm/terminal.c
    ${REPO_ROOT}/lib/platform/wasm/threading.c
    ${REPO_ROOT}/lib/platform/wasm/environment.c
    ${REPO_ROOT}/lib/platform/wasm/time.c
    ${REPO_ROOT}/lib/platform/wasm/init.c

    # Core module
    ${REPO_ROOT}/lib/core/error.c
    ${REPO_ROOT}/lib/log/logging.c

    # Util module (no PCRE2 files)
    ${REPO_ROOT}/lib/util/string.c
    ${REPO_ROOT}/lib/util/buffer.c
    ${REPO_ROOT}/lib/util/format.c

    # Debug/memory tracking
    ${REPO_ROOT}/lib/debug/memory.c

    # Video module (ASCII conversion only)
    ${REPO_ROOT}/lib/video/ascii.c
    ${REPO_ROOT}/lib/video/color_filter.c
    ${REPO_ROOT}/lib/video/ansi_fast.c
    ${REPO_ROOT}/lib/video/image.c
    ${REPO_ROOT}/lib/video/framebuffer.c
    ${REPO_ROOT}/lib/video/output_buffer.c

    # Options module (full)
    ${REPO_ROOT}/lib/options/options.c
    ${REPO_ROOT}/lib/options/rcu.c
    ${REPO_ROOT}/lib/options/common.c
    ${REPO_ROOT}/lib/options/parsers.c
    ${REPO_ROOT}/lib/options/strings.c
    ${REPO_ROOT}/lib/options/validation.c
    ${REPO_ROOT}/lib/options/builder.c
    ${REPO_ROOT}/lib/options/registry.c
    ${REPO_ROOT}/lib/options/config/presets.c
    ${REPO_ROOT}/lib/options/config/config.c
)

# ============================================================================
# WASM Target
# ============================================================================

add_executable(mirror ${WASM_SOURCES})

# Compiler flags
target_compile_options(mirror PRIVATE
    -O3
    -fno-exceptions
    -Wall -Wextra
)

# Emscripten linker flags
set(EMSCRIPTEN_LINK_FLAGS
    # WASM settings
    -s WASM=1
    -s MODULARIZE=1
    -s EXPORT_ES6=1
    -s EXPORT_NAME='MirrorWasmModule'

    # Memory settings
    -s ALLOW_MEMORY_GROWTH=1
    -s INITIAL_MEMORY=64MB
    -s MAXIMUM_MEMORY=512MB

    # Threading (single-threaded)
    -s USE_PTHREADS=0

    # Exported functions
    -s EXPORTED_FUNCTIONS='[
        "_mirror_init",
        "_mirror_cleanup",
        "_mirror_set_width",
        "_mirror_set_height",
        "_mirror_get_width",
        "_mirror_get_height",
        "_mirror_set_render_mode",
        "_mirror_get_render_mode",
        "_mirror_set_color_mode",
        "_mirror_get_color_mode",
        "_mirror_set_color_filter",
        "_mirror_get_color_filter",
        "_mirror_convert_frame",
        "_mirror_free_string",
        "_malloc",
        "_free"
    ]'

    # Exported runtime methods
    -s EXPORTED_RUNTIME_METHODS='[
        "ccall",
        "cwrap",
        "UTF8ToString",
        "getValue",
        "setValue",
        "HEAPU8"
    ]'

    # Optimization
    -O3
    --closure 1

    # Error handling
    -s ASSERTIONS=1
    -s SAFE_HEAP=0

    # EM_JS support
    -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE='[$Browser]'
)

set_target_properties(mirror PROPERTIES
    LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS}"
)

# ============================================================================
# Post-build: Copy to src/wasm/dist/
# ============================================================================

add_custom_command(TARGET mirror POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/../src/wasm/dist
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/mirror.js
        ${CMAKE_BINARY_DIR}/mirror.wasm
        ${CMAKE_SOURCE_DIR}/../src/wasm/dist/
    COMMENT "Copying WASM files to src/wasm/dist/"
)
```

**Step 2: Verify file created**

Run: `wc -l web/web.ascii-chat.com/wasm/CMakeLists.txt`
Expected: ~170 lines

**Step 3: Commit**

```bash
git add web/web.ascii-chat.com/wasm/CMakeLists.txt
git commit -m "feat(wasm): Add CMake build configuration for WASM target

Configure Emscripten build with:
- Conditional compilation flags (NO_NETWORK, NO_CRYPTO, etc.)
- Source file collection from main repo
- Exported functions list
- Optimization flags (-O3, closure compiler)
- Post-build copy to src/wasm/dist/

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 9: Build Script

**Files:**
- Create: `web/web.ascii-chat.com/wasm/build.sh`

**Step 1: Create build.sh**

Create `web/web.ascii-chat.com/wasm/build.sh`:

```bash
#!/bin/bash
# Build script for ascii-chat Mirror WASM

set -e

echo "Building ascii-chat Mirror WASM with full library..."

# Clean previous build
rm -rf build
mkdir -p build

# Configure with Emscripten
cd build
emcmake cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=emcc

# Build
emmake make -j$(nproc)

echo "✅ WASM build complete!"
echo "   Output: src/wasm/dist/mirror.{js,wasm}"
ls -lh ../src/wasm/dist/
```

**Step 2: Make executable**

Run: `chmod +x web/web.ascii-chat.com/wasm/build.sh`

**Step 3: Test build (will fail - missing source files expected)**

Run: `cd web/web.ascii-chat.com/wasm && ./build.sh`
Expected: CMake configures, build fails due to missing source files (this is OK)

**Step 4: Commit**

```bash
git add web/web.ascii-chat.com/wasm/build.sh
git commit -m "feat(wasm): Add build script for Emscripten compilation

Run with: cd web/web.ascii-chat.com/wasm && ./build.sh
Outputs mirror.js and mirror.wasm to src/wasm/dist/

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 10: TypeScript Type Definitions

**Files:**
- Create: `web/web.ascii-chat.com/src/wasm/mirror-types.ts`

**Step 1: Create wasm directory**

```bash
mkdir -p web/web.ascii-chat.com/src/wasm
```

**Step 2: Create mirror-types.ts**

Create `web/web.ascii-chat.com/src/wasm/mirror-types.ts`:

```typescript
// Type definitions for Mirror WASM module

import { Terminal } from 'xterm';

export enum RenderMode {
    Foreground = 0,
    Background = 1,
    HalfBlock = 2,
}

export enum ColorMode {
    Auto = 0,
    None = 1,
    Color16 = 2,
    Color256 = 3,
    TrueColor = 4,
}

export enum ColorFilter {
    None = 0,
    Black = 1,
    White = 2,
    Green = 3,
    Magenta = 4,
    Fuchsia = 5,
    Orange = 6,
    Teal = 7,
    Cyan = 8,
    PastelPink = 9,
    ErrorRed = 10,
    Yellow = 11,
}

export interface EmscriptenModule {
    HEAPU8: Uint8Array;
    UTF8ToString(ptr: number, maxBytesToRead?: number): string;
    ccall(ident: string, returnType: string, argTypes: string[], args: any[]): any;
    cwrap(ident: string, returnType: string, argTypes: string[]): Function;
    getValue(ptr: number, type: string): number;
    setValue(ptr: number, value: number, type: string): void;
}

export interface MirrorWasmModule extends EmscriptenModule {
    // Initialization
    _mirror_init(width: number, height: number): number;
    _mirror_cleanup(): void;

    // Dimension settings
    _mirror_set_width(width: number): number;
    _mirror_set_height(height: number): number;
    _mirror_get_width(): number;
    _mirror_get_height(): number;

    // Render settings
    _mirror_set_render_mode(mode: number): number;
    _mirror_get_render_mode(): number;
    _mirror_set_color_mode(mode: number): number;
    _mirror_get_color_mode(): number;
    _mirror_set_color_filter(filter: number): number;
    _mirror_get_color_filter(): number;

    // Frame conversion
    _mirror_convert_frame(
        rgba_ptr: number,
        src_width: number,
        src_height: number
    ): number;
    _mirror_free_string(ptr: number): void;

    // Memory management
    _malloc(size: number): number;
    _free(ptr: number): void;

    // xterm.js reference (for EM_JS callbacks)
    xterm: Terminal | null;
}
```

**Step 3: Verify file created**

Run: `wc -l web/web.ascii-chat.com/src/wasm/mirror-types.ts`
Expected: ~75 lines

**Step 4: Commit**

```bash
git add web/web.ascii-chat.com/src/wasm/mirror-types.ts
git commit -m "feat(wasm): Add TypeScript type definitions

Define enums for RenderMode, ColorMode, ColorFilter and interface
for MirrorWasmModule with all exported functions.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 11: TypeScript WASM Wrapper Class (Part 1)

**Files:**
- Create: `web/web.ascii-chat.com/src/wasm/mirror.ts`

**Step 1: Create mirror.ts with class definition**

Create `web/web.ascii-chat.com/src/wasm/mirror.ts`:

```typescript
// WASM wrapper for ascii-chat mirror mode

import { Terminal } from 'xterm';
import MirrorWasmModuleFactory from './dist/mirror.js';
import { MirrorWasmModule, RenderMode, ColorMode, ColorFilter } from './mirror-types';

export class MirrorWasm {
    private module: MirrorWasmModule | null = null;
    private initialized = false;

    async init(xterm: Terminal, width: number, height: number): Promise<void> {
        console.log('[MirrorWasm] Loading module...');
        this.module = await MirrorWasmModuleFactory();

        // Expose xterm.js to WASM (for EM_JS callbacks)
        this.module.xterm = xterm;

        // Initialize WASM module
        const result = this.module._mirror_init(width, height);
        if (result !== 0) {
            throw new Error('Failed to initialize WASM module');
        }

        this.initialized = true;
        console.log('[MirrorWasm] Module initialized');
    }

    cleanup(): void {
        if (this.module && this.initialized) {
            this.module._mirror_cleanup();
            this.initialized = false;
        }
    }

    private checkInitialized(): void {
        if (!this.initialized || !this.module) {
            throw new Error('WASM module not initialized. Call init() first.');
        }
    }

    // Dimension settings - continued in next step...
}
```

**Step 2: Verify file created**

Run: `wc -l web/web.ascii-chat.com/src/wasm/mirror.ts`
Expected: ~40 lines

**Step 3: Commit**

```bash
git add web/web.ascii-chat.com/src/wasm/mirror.ts
git commit -m "feat(wasm): Add WASM wrapper class skeleton

Implement MirrorWasm class with init, cleanup, and checkInitialized.
Exposes xterm instance to WASM via Module.xterm.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 12: TypeScript WASM Wrapper Class (Part 2)

**Files:**
- Modify: `web/web.ascii-chat.com/src/wasm/mirror.ts`

**Step 1: Add dimension methods**

Append to `MirrorWasm` class in `mirror.ts`:

```typescript
    // Dimension settings
    async setWidth(width: number): Promise<void> {
        this.checkInitialized();
        const result = this.module!._mirror_set_width(width);
        if (result !== 0) {
            throw new Error(`Failed to set width to ${width}`);
        }
    }

    async setHeight(height: number): Promise<void> {
        this.checkInitialized();
        const result = this.module!._mirror_set_height(height);
        if (result !== 0) {
            throw new Error(`Failed to set height to ${height}`);
        }
    }

    getWidth(): number {
        this.checkInitialized();
        return this.module!._mirror_get_width();
    }

    getHeight(): number {
        this.checkInitialized();
        return this.module!._mirror_get_height();
    }
```

**Step 2: Add render mode methods**

Continue appending:

```typescript
    // Render settings
    async setRenderMode(mode: RenderMode): Promise<void> {
        this.checkInitialized();
        const result = this.module!._mirror_set_render_mode(mode);
        if (result !== 0) {
            throw new Error(`Failed to set render mode to ${mode}`);
        }
    }

    getRenderMode(): RenderMode {
        this.checkInitialized();
        return this.module!._mirror_get_render_mode();
    }

    async setColorMode(mode: ColorMode): Promise<void> {
        this.checkInitialized();
        const result = this.module!._mirror_set_color_mode(mode);
        if (result !== 0) {
            throw new Error(`Failed to set color mode to ${mode}`);
        }
    }

    getColorMode(): ColorMode {
        this.checkInitialized();
        return this.module!._mirror_get_color_mode();
    }

    async setColorFilter(filter: ColorFilter): Promise<void> {
        this.checkInitialized();
        const result = this.module!._mirror_set_color_filter(filter);
        if (result !== 0) {
            throw new Error(`Failed to set color filter to ${filter}`);
        }
    }

    getColorFilter(): ColorFilter {
        this.checkInitialized();
        return this.module!._mirror_get_color_filter();
    }
```

**Step 3: Verify additions**

Run: `wc -l web/web.ascii-chat.com/src/wasm/mirror.ts`
Expected: ~100 lines

**Step 4: Commit**

```bash
git add web/web.ascii-chat.com/src/wasm/mirror.ts
git commit -m "feat(wasm): Add dimension and render settings methods

Implement:
- setWidth/setHeight/getWidth/getHeight
- setRenderMode/getRenderMode
- setColorMode/getColorMode
- setColorFilter/getColorFilter

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 13: TypeScript WASM Wrapper Class (Part 3)

**Files:**
- Modify: `web/web.ascii-chat.com/src/wasm/mirror.ts`

**Step 1: Add frame conversion method**

Append to `MirrorWasm` class:

```typescript
    // Frame conversion
    convertFrame(rgbaData: Uint8Array, srcWidth: number, srcHeight: number): string {
        this.checkInitialized();

        // Allocate WASM memory for RGBA data
        const dataPtr = this.module!._malloc(rgbaData.length);
        if (!dataPtr) {
            throw new Error('Failed to allocate WASM memory');
        }

        try {
            // Copy RGBA data to WASM memory
            this.module!.HEAPU8.set(rgbaData, dataPtr);

            // Call WASM conversion function
            const resultPtr = this.module!._mirror_convert_frame(
                dataPtr,
                srcWidth,
                srcHeight
            );

            if (!resultPtr) {
                throw new Error('WASM convert_frame returned null');
            }

            // Convert C string to JavaScript string
            const asciiString = this.module!.UTF8ToString(resultPtr);

            // Free the result buffer
            this.module!._mirror_free_string(resultPtr);

            return asciiString;
        } finally {
            // Always free the input buffer
            this.module!._free(dataPtr);
        }
    }
```

**Step 2: Add singleton and export functions**

Append after the class definition:

```typescript
// Singleton instance
let wasmInstance: MirrorWasm | null = null;

export async function initMirrorWasm(
    xterm: Terminal,
    width: number,
    height: number
): Promise<MirrorWasm> {
    if (!wasmInstance) {
        wasmInstance = new MirrorWasm();
        await wasmInstance.init(xterm, width, height);
    }
    return wasmInstance;
}

export function isWasmReady(): boolean {
    return wasmInstance !== null;
}

// Re-export types
export { RenderMode, ColorMode, ColorFilter };
```

**Step 3: Verify file complete**

Run: `wc -l web/web.ascii-chat.com/src/wasm/mirror.ts`
Expected: ~160 lines

**Step 4: Commit**

```bash
git add web/web.ascii-chat.com/src/wasm/mirror.ts
git commit -m "feat(wasm): Add frame conversion and singleton exports

Implement convertFrame with proper memory management:
- Allocate WASM memory for RGBA data
- Call mirror_convert_frame
- Free allocated memory in finally block

Add singleton pattern with initMirrorWasm and isWasmReady.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 14: Settings Panel Component

**Files:**
- Create: `web/web.ascii-chat.com/src/components/MirrorSettings.tsx`

**Step 1: Create components directory**

```bash
mkdir -p web/web.ascii-chat.com/src/components
```

**Step 2: Create MirrorSettings.tsx (structure only)**

Create `web/web.ascii-chat.com/src/components/MirrorSettings.tsx`:

```typescript
import { useState, useEffect } from 'react';
import { MirrorWasm, RenderMode, ColorMode, ColorFilter } from '../wasm/mirror';

interface MirrorSettingsProps {
    wasm: MirrorWasm | null;
    isRunning: boolean;
    onDimensionsChange?: (width: number, height: number) => void;
}

export function MirrorSettings({ wasm, isRunning, onDimensionsChange }: MirrorSettingsProps) {
    const [isOpen, setIsOpen] = useState(true);

    // Settings state
    const [width, setWidth] = useState(150);
    const [height, setHeight] = useState(60);
    const [renderMode, setRenderMode] = useState(RenderMode.Foreground);
    const [colorMode, setColorMode] = useState(ColorMode.Auto);
    const [colorFilter, setColorFilter] = useState(ColorFilter.None);

    // Load initial values from WASM
    useEffect(() => {
        if (wasm) {
            setWidth(wasm.getWidth());
            setHeight(wasm.getHeight());
            setRenderMode(wasm.getRenderMode());
            setColorMode(wasm.getColorMode());
            setColorFilter(wasm.getColorFilter());
        }
    }, [wasm]);

    // Event handlers - continued in next step...

    return null; // UI rendering in next step
}
```

**Step 3: Verify file created**

Run: `wc -l web/web.ascii-chat.com/src/components/MirrorSettings.tsx`
Expected: ~35 lines

**Step 4: Commit**

```bash
git add web/web.ascii-chat.com/src/components/MirrorSettings.tsx
git commit -m "feat(ui): Add MirrorSettings component skeleton

Define props, state, and useEffect for loading settings from WASM.
UI rendering to be added in next step.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 15: Settings Panel Event Handlers

**Files:**
- Modify: `web/web.ascii-chat.com/src/components/MirrorSettings.tsx`

**Step 1: Add event handlers**

Add before the `return` statement in `MirrorSettings.tsx`:

```typescript
    // Event handlers
    const handleWidthChange = async (newWidth: number) => {
        if (!wasm) return;
        setWidth(newWidth);
        await wasm.setWidth(newWidth);
        onDimensionsChange?.(newWidth, height);
    };

    const handleHeightChange = async (newHeight: number) => {
        if (!wasm) return;
        setHeight(newHeight);
        await wasm.setHeight(newHeight);
        onDimensionsChange?.(width, newHeight);
    };

    const handleRenderModeChange = async (mode: RenderMode) => {
        if (!wasm) return;
        setRenderMode(mode);
        await wasm.setRenderMode(mode);
    };

    const handleColorModeChange = async (mode: ColorMode) => {
        if (!wasm) return;
        setColorMode(mode);
        await wasm.setColorMode(mode);
    };

    const handleColorFilterChange = async (filter: ColorFilter) => {
        if (!wasm) return;
        setColorFilter(filter);
        await wasm.setColorFilter(filter);
    };
```

**Step 2: Verify additions**

Run: `grep -c "handleWidthChange\|handleHeightChange\|handleRenderModeChange" web/web.ascii-chat.com/src/components/MirrorSettings.tsx`
Expected: 5 (5 handler functions)

**Step 3: Commit**

```bash
git add web/web.ascii-chat.com/src/components/MirrorSettings.tsx
git commit -m "feat(ui): Add settings panel event handlers

Implement handlers for width, height, render mode, color mode,
and color filter changes. Each handler updates local state and
calls WASM setter.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 16: Settings Panel UI Rendering

**Files:**
- Modify: `web/web.ascii-chat.com/src/components/MirrorSettings.tsx`

**Step 1: Replace return statement with UI**

Replace `return null;` with:

```typescript
    // Collapsed state
    if (!isOpen) {
        return (
            <button
                onClick={() => setIsOpen(true)}
                className="fixed top-20 right-4 px-4 py-2 bg-terminal-8 text-terminal-fg rounded shadow-lg hover:bg-terminal-7"
            >
                Settings
            </button>
        );
    }

    // Expanded panel
    return (
        <div className="fixed top-16 right-4 w-80 bg-terminal-bg/95 border border-terminal-8 rounded p-4 shadow-lg z-50">
            <div className="flex items-center justify-between mb-4">
                <h3 className="text-lg font-semibold">Mirror Settings</h3>
                <button
                    onClick={() => setIsOpen(false)}
                    className="text-terminal-8 hover:text-terminal-fg text-xl leading-none"
                >
                    ✕
                </button>
            </div>

            {/* Dimensions */}
            <div className="mb-4">
                <label className="block text-sm mb-2 text-terminal-8">Dimensions</label>
                <div className="flex gap-2">
                    <input
                        type="number"
                        value={width}
                        onChange={(e) => handleWidthChange(Number(e.target.value))}
                        className="flex-1 px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-terminal-fg"
                        min="10"
                        max="500"
                    />
                    <span className="self-center text-terminal-8">×</span>
                    <input
                        type="number"
                        value={height}
                        onChange={(e) => handleHeightChange(Number(e.target.value))}
                        className="flex-1 px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-terminal-fg"
                        min="10"
                        max="300"
                    />
                </div>
            </div>

            {/* Render Mode */}
            <div className="mb-4">
                <label className="block text-sm mb-2 text-terminal-8">Render Mode</label>
                <select
                    value={renderMode}
                    onChange={(e) => handleRenderModeChange(Number(e.target.value))}
                    className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-terminal-fg"
                >
                    <option value={RenderMode.Foreground}>Foreground</option>
                    <option value={RenderMode.Background}>Background</option>
                    <option value={RenderMode.HalfBlock}>Half-block</option>
                </select>
            </div>

            {/* Color Filter */}
            <div className="mb-4">
                <label className="block text-sm mb-2 text-terminal-8">Color Filter</label>
                <select
                    value={colorFilter}
                    onChange={(e) => handleColorFilterChange(Number(e.target.value))}
                    className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-terminal-fg"
                >
                    <option value={ColorFilter.None}>None</option>
                    <option value={ColorFilter.Green}>Green</option>
                    <option value={ColorFilter.Cyan}>Cyan</option>
                    <option value={ColorFilter.Magenta}>Magenta</option>
                    <option value={ColorFilter.Orange}>Orange</option>
                    <option value={ColorFilter.Yellow}>Yellow</option>
                </select>
            </div>

            {/* Color Mode */}
            <div className="mb-4">
                <label className="block text-sm mb-2 text-terminal-8">Color Mode</label>
                <select
                    value={colorMode}
                    onChange={(e) => handleColorModeChange(Number(e.target.value))}
                    className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-terminal-fg"
                >
                    <option value={ColorMode.Auto}>Auto</option>
                    <option value={ColorMode.None}>None</option>
                    <option value={ColorMode.Color16}>16-color</option>
                    <option value={ColorMode.Color256}>256-color</option>
                    <option value={ColorMode.TrueColor}>Truecolor</option>
                </select>
            </div>
        </div>
    );
```

**Step 2: Verify file complete**

Run: `wc -l web/web.ascii-chat.com/src/components/MirrorSettings.tsx`
Expected: ~150 lines

**Step 3: Commit**

```bash
git add web/web.ascii-chat.com/src/components/MirrorSettings.tsx
git commit -m "feat(ui): Add settings panel UI rendering

Implement floating overlay panel with:
- Collapsible button when closed
- Dimensions inputs (width x height)
- Render mode dropdown
- Color filter dropdown
- Color mode dropdown

Styled with Tailwind CSS, semi-transparent background.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 17: Documentation

**Files:**
- Modify: `docs/plans/2026-02-06-web-mirror-full-library-wasm.md`

**Step 1: Update design document status**

Change line 3 in design doc from:
```markdown
**Status:** Design Phase
```

To:
```markdown
**Status:** ✅ Implementation Complete
```

**Step 2: Add implementation notes section**

Append to end of design document:

```markdown

---

## Implementation Notes

**Completed:** 2026-02-06

### Deviations from Plan

None - implementation followed design exactly.

### Build Instructions

```bash
# Build WASM module
cd web/web.ascii-chat.com/wasm
./build.sh

# Outputs:
# - src/wasm/dist/mirror.js (~200-300KB)
# - src/wasm/dist/mirror.wasm (~150-250KB)
```

### Testing Checklist

- [ ] WASM module builds without errors
- [ ] Module loads in browser
- [ ] All exported functions accessible
- [ ] Dimension settings update correctly
- [ ] Render mode changes work
- [ ] Color filter applies correctly
- [ ] Color mode switches work
- [ ] Frame conversion produces ASCII output
- [ ] No memory leaks over time
- [ ] Settings persist across renders
```

**Step 3: Commit**

```bash
git add docs/plans/2026-02-06-web-mirror-full-library-wasm.md
git commit -m "docs: Mark WASM implementation complete

Update design document status and add implementation notes.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Success Criteria

✅ **Phase 1 Complete When:**
- All platform layer files created (5 files in `lib/platform/wasm/`)
- WASM entry point complete (`mirror_wasm.c` with 12 exported functions)
- CMake build configuration ready
- Build script executable

✅ **Phase 2 Complete When:**
- TypeScript types defined (`mirror-types.ts`)
- WASM wrapper class complete (`mirror.ts`)
- Settings panel component finished (`MirrorSettings.tsx`)

✅ **Phase 3 Complete When:**
- WASM builds successfully (`./build.sh` produces `.wasm` and `.js`)
- All 17 commits pushed to git
- Design document marked complete

---

## Next Steps After This Plan

After completing this implementation plan, you'll need to:

1. **Integrate with Mirror.tsx** - Update existing Mirror page to use new WASM module
2. **Test in browser** - Verify WASM loads and functions work
3. **Fix build errors** - Address any compilation issues
4. **Performance testing** - Measure FPS and memory usage
5. **Cross-browser testing** - Test on Chrome, Firefox, Safari

**Use @superpowers:executing-plans or @superpowers:subagent-driven-development to implement this plan task-by-task.**
