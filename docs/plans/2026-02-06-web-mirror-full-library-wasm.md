# Web Mirror Mode: Full Library WASM Build with Settings Panel

**Date:** 2026-02-06
**Status:** Design Phase
**Goal:** Enable web.ascii-chat.com/mirror to use the full ascii-chat library via WASM with interactive settings panel for render-mode, color-filter, color-mode, width, and height.

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Module Dependencies](#module-dependencies)
4. [Platform Layer: WASM Implementation](#platform-layer-wasm-implementation)
5. [Exported WASM API](#exported-wasm-api)
6. [Build Configuration](#build-configuration)
7. [TypeScript Integration](#typescript-integration)
8. [UI Implementation](#ui-implementation)
9. [Implementation Plan](#implementation-plan)

---

## Overview

### Current State

The web mirror (`web.ascii-chat.com/mirror`) currently uses a minimal standalone WASM implementation:
- **File:** `web/web.ascii-chat.com/wasm/mirror_standalone.c` (71 lines)
- **Features:** Basic luminance-to-ASCII conversion only
- **Palette:** Fixed `"   ...',;:clodxkO0KXNWM"`
- **No settings:** No render modes, color filters, or color modes

### Target State

Build the full ascii-chat library as WASM with:
- ✅ Complete options system (RCU-based, thread-safe)
- ✅ Full video rendering pipeline (color filters, render modes, ANSI output)
- ✅ Runtime-configurable settings via `options_set_int()`
- ✅ Interactive settings panel in web UI
- ✅ Auto-calculated dimensions from terminal viewport + webcam aspect ratio
- ✅ Manual dimension override available

### Design Philosophy

**Full library integration from the start** - Build with threading support (Emscripten pthreads) to enable proper `options_set_int()` integration, even though we'll start in single-threaded mode (pthreads as no-ops). This gives us a clear upgrade path to true multithreading later if needed.

---

## Architecture

### High-Level Structure

```
web/web.ascii-chat.com/wasm/
├── CMakeLists.txt          # New WASM build config (Emscripten)
├── mirror_wasm.c           # WASM entry point (replaces mirror_standalone.c)
├── build.sh                # Build script (emcmake + emmake)
├── platform_wasm/          # WASM-specific platform layer (NEW)
│   ├── terminal.c          # EM_JS bridge to xterm.js
│   ├── threading.c         # Emscripten pthread wrappers
│   ├── environment.c       # getenv, strerror stubs
│   ├── time.c              # Emscripten time functions
│   └── init.c              # Platform initialization
└── build/                  # Emscripten build output
    ├── mirror.js           # Generated glue code (~200-300KB)
    └── mirror.wasm         # Compiled library (~150-250KB)
```

### Module Selection

Build these ascii-chat modules for WASM:

| Module | Status | Notes |
|--------|--------|-------|
| `ascii-chat-core` | ✅ Full | Error handling, logging, memory tracking |
| `ascii-chat-util` | ⚠️ Partial | Exclude PCRE2-dependent files (url.c) |
| `ascii-chat-platform-wasm` | ✅ New | WASM-specific platform implementations |
| `ascii-chat-video` | ⚠️ Partial | ASCII conversion, color filters, render modes (no FFmpeg/V4L2) |
| `ascii-chat-options` | ✅ Full | Complete options system with RCU threading |

**Excluded modules:**
- ❌ `ascii-chat-network` - No networking in mirror mode
- ❌ `ascii-chat-crypto` - No encryption needed
- ❌ `ascii-chat-audio` - Mirror is video-only
- ❌ `ascii-chat-discovery` - No session discovery

### Thread Model

**Phase 1 (Initial):** Single-threaded WASM
- Emscripten pthreads disabled: `-s USE_PTHREADS=0`
- Mutexes compile as no-ops
- No SharedArrayBuffer required (simpler deployment)
- No COOP/COEP headers needed on web server

**Phase 2 (Future):** Multi-threaded WASM (optional upgrade)
- Enable: `-s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=2`
- Requires SharedArrayBuffer (COOP/COEP headers)
- True RCU read-copy-update semantics
- Web Worker pool for background processing

---

## Module Dependencies

### Dependency Matrix

| Dependency | Native Build | WASM Build | Strategy |
|------------|--------------|------------|----------|
| **libsodium** | Required | ❌ Excluded | `-DNO_CRYPTO=1` |
| **PCRE2** | Required | ❌ Excluded | `-DNO_PCRE2=1` (start without, add later if needed) |
| **pthread** | Required | ✅ Emscripten | `-s USE_PTHREADS=0` (no-op mutexes in single-threaded) |
| **FFmpeg** | Optional | ❌ Excluded | `-DNO_FFMPEG=1` |
| **PortAudio** | Required | ❌ Excluded | `-DNO_AUDIO=1` |
| **Opus** | Required | ❌ Excluded | `-DNO_AUDIO=1` |
| **V4L2/AVFoundation** | Platform | ❌ Excluded | `-DNO_WEBCAM_CAPTURE=1` |
| **BearSSL** | Optional | ❌ Excluded | `-DNO_CRYPTO=1` |
| **WebRTC AEC** | Optional | ❌ Excluded | `-DNO_AUDIO=1` |

### PCRE2 Strategy

**Start WITHOUT PCRE2:**
- Options parsing doesn't need it (uses simple string comparisons)
- URL validation not needed for mirror mode
- YouTube video ID extraction not needed
- Saves ~150KB in WASM binary

**Add PCRE2 later if needed:**
1. Emscripten port: `embuilder build pcre2`
2. Or build from source with Emscripten toolchain
3. Useful for future features (URL streaming, regex validation)

### Conditional Compilation Flags

```c
// Defined in CMakeLists.txt, used in source code
#define EMSCRIPTEN_BUILD 1
#define WASM_MIRROR_MODE 1
#define NO_NETWORK 1
#define NO_CRYPTO 1
#define NO_AUDIO 1
#define NO_FFMPEG 1
#define NO_WEBCAM_CAPTURE 1
#define NO_PCRE2 1
#define PCRE2_CODE_UNIT_WIDTH 8  // Keep for header compatibility
```

### Source Files Compiled

```cmake
set(WASM_SOURCES
    # Entry point
    ${CMAKE_SOURCE_DIR}/mirror_wasm.c

    # Platform layer (WASM-specific - NEW)
    ${REPO_ROOT}/lib/platform/wasm/terminal.c
    ${REPO_ROOT}/lib/platform/wasm/threading.c
    ${REPO_ROOT}/lib/platform/wasm/environment.c
    ${REPO_ROOT}/lib/platform/wasm/time.c
    ${REPO_ROOT}/lib/platform/wasm/init.c

    # Core module
    ${REPO_ROOT}/lib/core/error.c
    ${REPO_ROOT}/lib/core/logging.c
    ${REPO_ROOT}/lib/log/logging.c

    # Util module (exclude PCRE2 files)
    ${REPO_ROOT}/lib/util/string.c
    ${REPO_ROOT}/lib/util/buffer.c
    ${REPO_ROOT}/lib/util/format.c
    # EXCLUDE: lib/util/url.c (uses PCRE2)

    # Debug/memory tracking
    ${REPO_ROOT}/lib/debug/memory.c

    # Video module (ASCII conversion only)
    ${REPO_ROOT}/lib/video/ascii.c
    ${REPO_ROOT}/lib/video/color_filter.c
    ${REPO_ROOT}/lib/video/ansi_fast.c
    ${REPO_ROOT}/lib/video/image.c
    ${REPO_ROOT}/lib/video/framebuffer.c
    ${REPO_ROOT}/lib/video/output_buffer.c
    # EXCLUDE: lib/media/ffmpeg_decoder.c (uses FFmpeg)
    # EXCLUDE: lib/media/webcam.c (uses V4L2/AVFoundation)
    # EXCLUDE: lib/media/youtube.c (uses PCRE2 + FFmpeg)

    # Options module (FULL)
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
```

---

## Platform Layer: WASM Implementation

### Overview

Create `lib/platform/wasm/` directory with WASM-specific implementations of the platform abstraction API defined in `include/ascii-chat/platform/abstraction.h`.

**Key principle:** Library code remains unchanged - it just calls `platform_*()` functions, and the WASM platform provides the implementation.

### Directory Structure

```
lib/platform/
├── posix/          # Existing (Linux/macOS)
├── windows/        # Existing (Windows)
└── wasm/           # NEW - WASM/Emscripten implementations
    ├── terminal.c  # EM_JS bridge to xterm.js
    ├── threading.c # Emscripten pthread wrappers
    ├── environment.c # getenv, strerror
    ├── time.c      # Emscripten time functions
    └── init.c      # Platform initialization
```

### Terminal Integration (Bidirectional Bridge)

**WASM → JavaScript (via EM_JS):**

```c
// lib/platform/wasm/terminal.c
#include <emscripten.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>

// EM_JS: Call JavaScript from C
EM_JS(int, js_get_terminal_cols, (), {
    return Module.xterm ? Module.xterm.cols : 80;
});

EM_JS(int, js_get_terminal_rows, (), {
    return Module.xterm ? Module.xterm.rows : 24;
});

EM_JS(int, js_get_color_mode, (), {
    // Query xterm.js theme/capabilities
    if (!Module.xterm) return 0; // COLOR_MODE_AUTO
    // Could check Module.xterm.options.theme for truecolor support
    return 4; // COLOR_MODE_TRUECOLOR (xterm.js supports it)
});

EM_JS(void, js_terminal_write, (const char* data, int len), {
    if (Module.xterm) {
        Module.xterm.write(UTF8ToString(data, len));
    }
});

// Platform API implementation
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
```

**JavaScript → WASM (Module initialization):**

```typescript
// src/wasm/mirror.ts
export async function initMirrorWasm(xtermInstance: Terminal): Promise<MirrorWasmModule> {
    const module = await MirrorWasmModuleFactory();

    // Expose xterm.js instance to WASM via Module.xterm
    module.xterm = xtermInstance;

    // WASM can now call js_get_terminal_cols(), js_get_terminal_rows(), etc.
    return module;
}
```

**Benefits:**
- ✅ Real terminal capability detection (query xterm.js configuration)
- ✅ Dynamic dimension updates (WASM sees xterm.js resize events)
- ✅ Direct ANSI output (WASM writes directly to xterm.js)
- ✅ Color mode synchronization (xterm.js theme ↔ WASM color_mode)

### Threading Implementation

```c
// lib/platform/wasm/threading.c
#include <ascii-chat/platform/abstraction.h>
#include <pthread.h>

// Emscripten provides pthread.h
// In single-threaded mode (USE_PTHREADS=0), these are no-ops
// In multi-threaded mode (USE_PTHREADS=1), these are real mutexes

int mutex_init(mutex_t *mutex) {
    return pthread_mutex_init(mutex, NULL);
}

int mutex_lock(mutex_t *mutex) {
    return pthread_mutex_lock(mutex);
}

int mutex_unlock(mutex_t *mutex) {
    return pthread_mutex_unlock(mutex);
}

int mutex_destroy(mutex_t *mutex) {
    return pthread_mutex_destroy(mutex);
}

// Thread creation (not used in mirror mode, but provided for completeness)
int asciichat_thread_create(asciichat_thread_t *thread, void *(*start_routine)(void *), void *arg) {
    return pthread_create(thread, NULL, start_routine, arg);
}

int asciichat_thread_join(asciichat_thread_t thread, void **retval) {
    return pthread_join(thread, retval);
}
```

### Environment and Utilities

```c
// lib/platform/wasm/environment.c
#include <ascii-chat/platform/abstraction.h>
#include <stdlib.h>
#include <string.h>

const char* platform_getenv(const char *name) {
    // Emscripten supports getenv() via environment variables passed at initialization
    return getenv(name);
}

const char* platform_strerror(int errnum) {
    return strerror(errnum);
}

// lib/platform/wasm/time.c
#include <ascii-chat/platform/abstraction.h>
#include <time.h>
#include <sys/time.h>

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

// lib/platform/wasm/init.c
#include <ascii-chat/platform/init.h>

asciichat_error_t platform_init(void) {
    // No special initialization needed for WASM
    return ASCIICHAT_OK;
}

void platform_cleanup(void) {
    // No cleanup needed
}
```

---

## Exported WASM API

### C Entry Point

```c
// src/wasm/mirror_wasm.c (NEW - replaces mirror_standalone.c)
#include <emscripten.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/color_filter.h>
#include <ascii-chat/video/framebuffer.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>

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
    // Options system expects at least mode name
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
// Settings API (Thread-safe via RCU)
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_width(int width) {
    if (width <= 0 || width > 1000) {
        return -1; // Invalid range
    }
    asciichat_error_t err = options_set_int("width", width);
    return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_set_height(int height) {
    if (height <= 0 || height > 1000) {
        return -1; // Invalid range
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

// ============================================================================
// Frame Conversion API
// ============================================================================

EMSCRIPTEN_KEEPALIVE
char* mirror_convert_frame(
    uint8_t *rgba_data,
    int src_width,
    int src_height
) {
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
    // This uses the real ascii.c conversion with proper sampling
    asciichat_error_t err = convert_image_to_ascii(
        rgba_data, src_width, src_height,
        &fb, render_mode
    );

    if (err != ASCIICHAT_OK) {
        framebuffer_cleanup(&fb);
        return NULL;
    }

    // Generate ANSI escape sequences from framebuffer
    // Returns allocated string with full ANSI output
    char *output = generate_ansi_output(&fb, render_mode);

    framebuffer_cleanup(&fb);
    return output;
}

EMSCRIPTEN_KEEPALIVE
void mirror_free_string(char *ptr) {
    SAFE_FREE(ptr);
}
```

### Exported Functions List

```javascript
// Exported to JavaScript via EXPORTED_FUNCTIONS
_mirror_init           // Initialize WASM module and options system
_mirror_cleanup        // Cleanup resources

_mirror_set_width      // Set terminal width
_mirror_set_height     // Set terminal height
_mirror_get_width      // Get current width
_mirror_get_height     // Get current height

_mirror_set_render_mode   // Set render mode (0=fg, 1=bg, 2=half-block)
_mirror_get_render_mode   // Get current render mode

_mirror_set_color_mode    // Set color mode (0=auto, 1=none, 2=16, 3=256, 4=truecolor)
_mirror_get_color_mode    // Get current color mode

_mirror_set_color_filter  // Set color filter (0=none, 3=green, etc.)
_mirror_get_color_filter  // Get current color filter

_mirror_convert_frame     // Convert RGBA frame to ASCII with current settings
_mirror_free_string       // Free string returned by convert_frame

_malloc                   // Allocate WASM memory
_free                     // Free WASM memory
```

---

## Build Configuration

### CMakeLists.txt

```cmake
# web/web.ascii-chat.com/wasm/CMakeLists.txt
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

    # Core module (minimal subset)
    ${REPO_ROOT}/lib/core/error.c
    ${REPO_ROOT}/lib/core/logging.c
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
    -O3                          # Optimize for size and speed
    -fno-exceptions              # No C++ exceptions
    -Wall -Wextra                # Enable warnings
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

    # Threading (single-threaded for now)
    -s USE_PTHREADS=0            # Pthreads as no-ops
    # Future: -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=2

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
    --closure 1                  # Google Closure Compiler

    # Error handling (development vs production)
    -s ASSERTIONS=1              # Enable for development
    -s SAFE_HEAP=0               # Disable for production

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

### Build Script

```bash
#!/bin/bash
# web/web.ascii-chat.com/wasm/build.sh

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

### Expected Output

```
src/wasm/dist/
├── mirror.js      (~200-300KB - Emscripten glue code)
└── mirror.wasm    (~150-250KB - Compiled C library)
```

Total size: **~350-550KB** (compressed with gzip: ~100-150KB)

---

## TypeScript Integration

### Type Definitions

```typescript
// src/wasm/mirror-types.ts

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

### WASM Wrapper Class

```typescript
// src/wasm/mirror.ts
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

    private checkInitialized(): void {
        if (!this.initialized || !this.module) {
            throw new Error('WASM module not initialized. Call init() first.');
        }
    }
}

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
```

---

## UI Implementation

### Settings Panel Design

**Layout:** Floating overlay panel (right side of screen)
- Collapsible via button
- Overlays the xterm.js terminal
- Semi-transparent background
- Auto-hides when webcam not running

**Controls:**
1. **Dimensions** (auto-calculated, editable)
   - Width input (number, live update)
   - Height input (number, live update)
   - "Reset to Auto" button

2. **Render Mode** (dropdown)
   - Foreground (default)
   - Background
   - Half-block

3. **Color Filter** (dropdown)
   - None (default)
   - Green, Cyan, Magenta, etc.

4. **Color Mode** (dropdown)
   - Auto (default)
   - None, 16-color, 256-color, Truecolor

### React Component

```typescript
// src/components/MirrorSettings.tsx
import { useState, useEffect } from 'react';
import { MirrorWasm } from '../wasm/mirror';
import { RenderMode, ColorMode, ColorFilter } from '../wasm/mirror-types';

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

    if (!isOpen) {
        return (
            <button
                onClick={() => setIsOpen(true)}
                className="fixed top-20 right-4 px-4 py-2 bg-terminal-8 text-terminal-fg rounded"
            >
                Settings
            </button>
        );
    }

    return (
        <div className="fixed top-16 right-4 w-80 bg-terminal-bg/95 border border-terminal-8 rounded p-4 shadow-lg">
            <div className="flex items-center justify-between mb-4">
                <h3 className="text-lg font-semibold">Mirror Settings</h3>
                <button onClick={() => setIsOpen(false)} className="text-terminal-8 hover:text-terminal-fg">
                    ✕
                </button>
            </div>

            {/* Dimensions */}
            <div className="mb-4">
                <label className="block text-sm mb-2">Dimensions</label>
                <div className="flex gap-2">
                    <input
                        type="number"
                        value={width}
                        onChange={(e) => handleWidthChange(Number(e.target.value))}
                        className="flex-1 px-2 py-1 bg-terminal-bg border border-terminal-8 rounded"
                        min="10"
                        max="500"
                    />
                    <span className="self-center">×</span>
                    <input
                        type="number"
                        value={height}
                        onChange={(e) => handleHeightChange(Number(e.target.value))}
                        className="flex-1 px-2 py-1 bg-terminal-bg border border-terminal-8 rounded"
                        min="10"
                        max="300"
                    />
                </div>
            </div>

            {/* Render Mode */}
            <div className="mb-4">
                <label className="block text-sm mb-2">Render Mode</label>
                <select
                    value={renderMode}
                    onChange={(e) => handleRenderModeChange(Number(e.target.value))}
                    className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded"
                >
                    <option value={RenderMode.Foreground}>Foreground</option>
                    <option value={RenderMode.Background}>Background</option>
                    <option value={RenderMode.HalfBlock}>Half-block</option>
                </select>
            </div>

            {/* Color Filter */}
            <div className="mb-4">
                <label className="block text-sm mb-2">Color Filter</label>
                <select
                    value={colorFilter}
                    onChange={(e) => handleColorFilterChange(Number(e.target.value))}
                    className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded"
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
                <label className="block text-sm mb-2">Color Mode</label>
                <select
                    value={colorMode}
                    onChange={(e) => handleColorModeChange(Number(e.target.value))}
                    className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded"
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
}
```

### Updated Mirror Page

```typescript
// src/pages/Mirror.tsx (updated)
import { useEffect, useRef, useState } from 'react'
import { Terminal } from 'xterm'
import { FitAddon } from '@xterm/addon-fit'
import 'xterm/css/xterm.css'
import { initMirrorWasm, MirrorWasm } from '../wasm/mirror'
import { MirrorSettings } from '../components/MirrorSettings'

export function MirrorPage() {
    const videoRef = useRef<HTMLVideoElement>(null)
    const canvasRef = useRef<HTMLCanvasElement>(null)
    const terminalRef = useRef<HTMLDivElement>(null)
    const xtermRef = useRef<Terminal | null>(null)
    const wasmRef = useRef<MirrorWasm | null>(null)

    const [fps, setFps] = useState<string>('--')
    const [isRunning, setIsRunning] = useState(false)
    const [error, setError] = useState<string>('')

    // Calculate dimensions from terminal viewport
    const calculateDimensions = () => {
        if (!terminalRef.current) return { width: 150, height: 60 };

        const rect = terminalRef.current.getBoundingClientRect();
        const charWidth = 8;   // Approximate character width in pixels
        const charHeight = 16; // Approximate character height in pixels

        const width = Math.floor(rect.width / charWidth);
        const height = Math.floor(rect.height / charHeight);

        return { width, height };
    };

    // Initialize WASM and xterm.js
    useEffect(() => {
        const terminal = new Terminal({
            theme: {
                background: '#0c0c0c',
                foreground: '#cccccc',
            },
            cursorStyle: 'block',
            cursorBlink: false,
            fontFamily: '"Courier New", Courier, monospace',
            fontSize: 12,
            scrollback: 0,
            disableStdin: true,
        })

        const fitAddon = new FitAddon()
        terminal.loadAddon(fitAddon)

        if (terminalRef.current) {
            terminal.open(terminalRef.current)
            fitAddon.fit()
            xtermRef.current = terminal

            // Initialize WASM with terminal dimensions
            const dims = calculateDimensions();
            initMirrorWasm(terminal, dims.width, dims.height)
                .then((wasm) => {
                    wasmRef.current = wasm;
                    console.log('[MirrorPage] WASM initialized');
                })
                .catch((err) => {
                    console.error('[MirrorPage] WASM init error:', err);
                    setError(`Failed to load WASM: ${err}`);
                });
        }

        return () => {
            if (wasmRef.current) {
                wasmRef.current.cleanup();
            }
            terminal.dispose();
        }
    }, [])

    const renderFrame = () => {
        const video = videoRef.current
        const canvas = canvasRef.current
        const wasm = wasmRef.current

        if (!video || !canvas || !wasm) return

        const ctx = canvas.getContext('2d', { willReadFrequently: true })
        if (!ctx) return

        // Draw video to canvas
        ctx.drawImage(video, 0, 0, canvas.width, canvas.height)

        // Get RGBA data
        const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height)

        // Convert via WASM (uses current settings from options system)
        const asciiArt = wasm.convertFrame(
            new Uint8Array(imageData.data),
            canvas.width,
            canvas.height
        )

        // Write to terminal
        if (xtermRef.current) {
            xtermRef.current.write('\x1b[H' + asciiArt)
        }
    }

    // ... rest of the component (startWebcam, stopWebcam, renderLoop)

    return (
        <div className="flex-1 bg-terminal-bg text-terminal-fg flex flex-col">
            {/* Hidden video/canvas */}
            <div style={{ opacity: 0, position: 'absolute', pointerEvents: 'none', width: 0, height: 0 }}>
                <video ref={videoRef} autoPlay muted playsInline />
                <canvas ref={canvasRef} />
            </div>

            {/* Controls */}
            <div className="px-4 py-3 flex-shrink-0 border-b border-terminal-8">
                <div className="flex items-center justify-between mb-2">
                    <h2 className="text-sm font-semibold">ASCII Mirror</h2>
                    <div className="text-xs text-terminal-8">
                        FPS: <span className="text-terminal-2">{fps}</span>
                    </div>
                </div>
                <div className="flex gap-2">
                    {!isRunning ? (
                        <button onClick={startWebcam} className="px-4 py-2 bg-terminal-2 text-terminal-bg rounded">
                            Start Webcam
                        </button>
                    ) : (
                        <button onClick={stopWebcam} className="px-4 py-2 bg-terminal-1 text-terminal-bg rounded">
                            Stop
                        </button>
                    )}
                </div>
            </div>

            {/* Terminal output (full width) */}
            <div className="flex-1 px-4 py-2">
                <div ref={terminalRef} className="w-full h-full rounded bg-terminal-bg" />
            </div>

            {/* Settings panel (floating overlay) */}
            <MirrorSettings
                wasm={wasmRef.current}
                isRunning={isRunning}
                onDimensionsChange={(width, height) => {
                    console.log(`Dimensions changed: ${width}x${height}`);
                }}
            />

            {error && (
                <div className="px-4 pb-2">
                    <div className="p-4 bg-terminal-1 text-terminal-fg rounded">{error}</div>
                </div>
            )}
        </div>
    )
}
```

---

## Implementation Plan

### Phase 1: Platform Layer (1-2 days)

**Tasks:**
1. Create `lib/platform/wasm/` directory
2. Implement:
   - `terminal.c` - EM_JS bridge to xterm.js
   - `threading.c` - Emscripten pthread wrappers
   - `environment.c` - getenv/strerror stubs
   - `time.c` - Time functions
   - `init.c` - Platform initialization
3. Add conditional compilation guards to existing platform code

**Validation:**
- Compiles without errors with Emscripten
- EM_JS functions can call JavaScript (test with simple console.log)

### Phase 2: WASM Build System (1 day)

**Tasks:**
1. Create `web/web.ascii-chat.com/wasm/CMakeLists.txt`
2. Create `mirror_wasm.c` entry point
3. Write `build.sh` script
4. Configure Emscripten linker flags
5. Test basic WASM build

**Validation:**
- `./build.sh` produces `mirror.wasm` and `mirror.js`
- WASM module loads in browser (basic test)
- Exported functions are accessible from JavaScript

### Phase 3: TypeScript Integration (1 day)

**Tasks:**
1. Create TypeScript type definitions
2. Implement `MirrorWasm` wrapper class
3. Update `Mirror.tsx` to use new WASM module
4. Test basic frame conversion

**Validation:**
- WASM module initializes successfully
- Frame conversion works (basic ASCII output)
- No memory leaks (check with Chrome DevTools)

### Phase 4: Settings Panel UI (1 day)

**Tasks:**
1. Create `MirrorSettings.tsx` component
2. Implement controls for all settings
3. Wire up event handlers to WASM setters
4. Add auto-dimension calculation from viewport
5. Style with Tailwind CSS

**Validation:**
- Settings panel opens/closes correctly
- All controls update WASM options
- Live updates reflect in ASCII output
- Dimensions auto-calculate on terminal resize

### Phase 5: Testing & Optimization (1 day)

**Tasks:**
1. Test all render modes (foreground, background, half-block)
2. Test all color filters (green, cyan, magenta, etc.)
3. Test all color modes (16, 256, truecolor)
4. Performance profiling (Chrome DevTools)
5. Memory leak detection
6. Cross-browser testing (Chrome, Firefox, Safari)

**Validation:**
- All settings work correctly
- Frame rate maintains 30-60 FPS
- No memory growth over time
- Works on major browsers

### Phase 6: Documentation & Cleanup (0.5 days)

**Tasks:**
1. Update README with WASM build instructions
2. Add comments to complex WASM code
3. Clean up debug logging
4. Update deployment documentation

**Total Estimated Time:** 5-6 days

---

## Success Criteria

✅ **Functional Requirements:**
- Full library options system working in WASM
- All settings configurable via UI (render-mode, color-filter, color-mode, width, height)
- Settings update in real-time (no page refresh)
- Auto-calculated dimensions from terminal viewport + webcam aspect ratio
- Manual dimension override available

✅ **Performance Requirements:**
- Frame rate: 30-60 FPS on modern hardware
- WASM binary size: < 500KB uncompressed (< 150KB gzipped)
- Memory usage: Stable (no leaks over time)
- Startup time: < 2 seconds to initialize WASM

✅ **Quality Requirements:**
- No regressions from current mirror mode behavior
- Cross-browser compatibility (Chrome, Firefox, Safari)
- Mobile responsive (settings panel collapses on small screens)
- Error handling for WASM initialization failures

---

## Future Enhancements

**Phase 2 Features (Post-MVP):**
1. **PCRE2 Support** - Add URL streaming (`--url` flag)
2. **Multi-threading** - Enable `USE_PTHREADS=1` for true concurrency
3. **SIMD Optimization** - Enable WASM SIMD for faster ASCII conversion
4. **Palette Customization** - Expose `--palette-custom` setting
5. **FPS Control** - Add FPS slider (currently hardcoded to 60)
6. **Export ASCII** - Download ASCII frames as text files
7. **Record Session** - Save ASCII video to file

**Performance Optimizations:**
1. Use `OffscreenCanvas` for video processing
2. Web Worker for WASM execution (offload from main thread)
3. WebGL-accelerated color filtering
4. Lazy loading of WASM module

---

## Appendix

### References

- **Emscripten Documentation:** https://emscripten.org/docs/
- **xterm.js API:** https://xtermjs.org/docs/api/
- **ascii-chat CLAUDE.md:** Full build system documentation
- **Platform Abstraction:** `include/ascii-chat/platform/abstraction.h`

### File Locations

```
New files:
├── lib/platform/wasm/             (5 files)
├── web/web.ascii-chat.com/wasm/
│   ├── CMakeLists.txt
│   ├── mirror_wasm.c
│   └── build.sh
├── web/web.ascii-chat.com/src/wasm/
│   ├── mirror.ts                  (WASM wrapper)
│   └── mirror-types.ts            (TypeScript types)
└── web/web.ascii-chat.com/src/components/
    └── MirrorSettings.tsx         (Settings UI)

Modified files:
├── web/web.ascii-chat.com/src/pages/Mirror.tsx
└── docs/plans/2026-02-06-web-mirror-full-library-wasm.md (this file)
```

---

## Sign-off

**Design Status:** ✅ Complete and validated
**Ready for Implementation:** Yes
**Estimated Timeline:** 5-6 days
**Next Step:** Begin Phase 1 (Platform Layer implementation)
