# ascii-chat iOS Client - Implementation Plan (WIP)

> **Status**: Draft / Research Phase
> **Last Updated**: 2026-03-05
> **Goal**: Native iOS app in Swift that uses libasciichat (compiled for iOS) to support server, client, mirror, and discovery modes.

---

## Overview

The iOS client follows the same architecture as the WASM web client: a thin native UI layer that delegates all protocol, crypto, rendering, and networking work to the C library. The Swift app is responsible for:

- Capturing camera frames (AVFoundation) and feeding RGBA pixels to the library
- Rendering ASCII output from the library to a display view
- Audio capture/playback (AVAudioEngine) bridged to the library's Opus pipeline
- Providing platform stubs where iOS differs from POSIX (terminal, filesystem, keepawake, etc.)

Everything else - ACIP protocol, crypto handshake, ASCII art conversion, color filters, packet framing, WebSocket transport, TCP transport, options system - comes from libasciichat.

---

## Architecture

```
┌──────────────────────────────────────────────────┐
│              Swift / SwiftUI App                 │
│  ┌──────────┐ ┌───────────┐ ┌──────────────────┐ │
│  │ Camera   │ │ ASCII     │ │ Settings /       │ │
│  │ Capture  │ │ Renderer  │ │ Mode Selection   │ │
│  │ (AVF)    │ │ View      │ │ UI               │ │
│  └────┬─────┘ └─────▲─────┘ └────────┬─────────┘ │
│       │             │                │           │
│  ┌────▼─────────────┴────────────────▼──────────┐│
│  │        Swift ↔ C Bridge (module map)         ││
│  └────┬─────────────┬────────────────┬──────────┘│
└───────┼─────────────┼────────────────┼───────────┘
        │             │                │
┌───────▼─────────────▼────────────────▼───────────┐
│                 libasciichat.a                   │
│  ┌─────────┐ ┌──────────┐ ┌────────┐ ┌─────────┐ │
│  │ Network │ │ Video /  │ │ Crypto │ │ Options │ │
│  │ TCP/WS  │ │ ASCII    │ │ Sodium │ │ System  │ │
│  │ ACIP    │ │ Render   │ │ E2E    │ │         │ │
│  └─────────┘ └──────────┘ └────────┘ └─────────┘ │
│  ┌─────────┐ ┌──────────┐ ┌────────┐ ┌─────────┐ │
│  │ Audio   │ │ Media    │ │ Discov │ │Platform │ │
│  │ Opus    │ │ FFmpeg   │ │ ACDS   │ │ iOS     │ │
│  └─────────┘ └──────────┘ └────────┘ └─────────┘ │
└──────────────────────────────────────────────────┘
```

### How it maps to the web client

| Web Client (TypeScript)           | iOS Client (Swift)                  |
|-----------------------------------|-------------------------------------|
| `useCanvasCapture` → RGBA pixels  | AVFoundation CVPixelBuffer → RGBA   |
| Canvas pixel rendering            | `FrameBufferView` (CGImage/Metal)   |
| `SocketBridge` (WebSocket)       | libasciichat WebSocket transport    |
| `ClientConnection` orchestrator  | `ConnectionManager` Swift class     |
| WASM `_malloc`/`_free`           | Direct C function calls via bridge  |
| `AudioPipeline` (Web Audio API)  | AVAudioEngine + library Opus codec  |
| React state / hooks              | SwiftUI `@Observable` / `@State`    |
| `requestAnimationFrame`          | `CADisplayLink` or `DispatchSource` |

---

## Workstreams

### 1. Compile libasciichat for iOS

**Goal**: Produce `libasciichat.a` (static library) for arm64 iOS and arm64 iOS Simulator.

#### 1a. iOS CMake toolchain file

Create `cmake/toolchains/iOS.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0")
# CMAKE_OSX_ARCHITECTURES set by preset (arm64)
# CMAKE_OSX_SYSROOT auto-detected by CMake when CMAKE_SYSTEM_NAME=iOS
set(CMAKE_CROSSCOMPILING TRUE)

# Use Clang (already project default)
set(CMAKE_C_COMPILER_TARGET arm-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET})

# Bitcode is dead (Xcode 14+), skip it
set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO)

# Framework search paths
set(CMAKE_FIND_FRAMEWORK FIRST)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

#### 1b. CMake options

Add `BUILD_IOS` and `BUILD_IOS_SIM` cache options (not presets):

```cmake
# In CMakeLists.txt or cmake/init/
option(BUILD_IOS "Build for iOS device (arm64)" OFF)
option(BUILD_IOS_SIM "Build for iOS Simulator (arm64)" OFF)

if(BUILD_IOS OR BUILD_IOS_SIM)
    set(PLATFORM_IOS TRUE)
    set(PLATFORM_POSIX TRUE)
    set(PLATFORM_DARWIN TRUE)
    set(CMAKE_CROSSCOMPILING TRUE)
    set(CMAKE_SYSTEM_NAME iOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0")
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_EXECUTABLES OFF)

    if(BUILD_IOS_SIM)
        set(CMAKE_OSX_SYSROOT "iphonesimulator")
    endif()

    message(STATUS "Platform: iOS ${BUILD_IOS_SIM ? \"Simulator\" : \"Device\"} (arm64)")
endif()
```

Usage:

```bash
# iOS device
cmake --preset default -B build-ios -DBUILD_IOS=ON
cmake --build build-ios

# iOS simulator
cmake --preset default -B build-ios-sim -DBUILD_IOS_SIM=ON
cmake --build build-ios-sim
```

#### 1c. Platform detection

Extend `cmake/init/PlatformDetection.cmake`:

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PLATFORM_IOS TRUE)
    set(PLATFORM_POSIX TRUE)
    set(PLATFORM_DARWIN TRUE)
    message(STATUS "Platform: iOS (${CMAKE_OSX_ARCHITECTURES})")
endif()
```

#### 1d. Dependency strategy

| Dependency     | iOS Strategy                                         | Notes                                   |
|----------------|------------------------------------------------------|-----------------------------------------|
| libsodium      | vcpkg `arm64-ios` or build from source               | Has official iOS support                |
| FFmpeg         | Build from source with `--enable-cross-compile`      | Mobile builds well-documented           |
| Opus           | vcpkg or source build                                | Lightweight, easy cross-compile         |
| libwebsockets  | Build from source (has `contrib/iOS.cmake`)          | Already in .deps-cache                  |
| libdatachannel | Build from source                                    | For WebRTC/discovery mode               |
| yyjson         | Header-only-ish, trivial                             | Just compiles                           |
| PCRE2          | vcpkg or source build                                | Standard cross-compile                  |
| BearSSL        | Build from source                                    | Small, portable                         |
| zstd           | vcpkg or source build                                | Easy cross-compile                      |
| sqlite3        | Use iOS system sqlite3                               | Ships with iOS                          |
| libvterm       | Build from source                                    | Small, no deps, easy cross-compile      |
| FreeType       | vcpkg or source build                                | Widely cross-compiled for iOS           |
| PortAudio      | **Skip** - use AVAudioEngine natively in iOS stubs   | PortAudio doesn't target iOS            |

**XCFramework target** (combine device + simulator):

```bash
xcodebuild -create-xcframework \
  -library build-ios/lib/libasciichat.a -headers include/ \
  -library build-ios-sim/lib/libasciichat.a -headers include/ \
  -output libasciichat.xcframework
```

---

### 2. iOS Platform Layer (`lib/platform/ios/`)

**Key insight**: iOS is Darwin, which is POSIX. Unlike WASM (which needs its own stubs for
everything because it runs in a browser), iOS can directly reuse the existing POSIX and macOS
platform layers. The iOS layer only needs to *override* the handful of things that differ.

Some stubs are genuinely shared between WASM and iOS (and future mobile/embedded targets) —
things like "no real TTY", "no fork/exec", "no PortAudio". These live in a new shared
`lib/platform/stubs/` directory that both `wasm/` and `ios/` pull from via CMake.

**Do NOT blindly copy the WASM stubs to iOS.** The WASM layer makes dangerous simplifications:
- WASM mutexes are **no-ops** (JS is single-threaded) — iOS is multithreaded, needs real pthreads
- WASM crypto stubs **auto-accept all hosts** and disable key verification — iOS needs full crypto
- WASM terminal code uses **EM_JS() bridges to JavaScript** — doesn't exist on iOS
- WASM logging routes to **browser console.log** — iOS needs os_log or NSLog

#### Inheritance strategy

```
lib/platform/posix/      ← iOS inherits everything here (pthreads, BSD sockets, time, strings, ...)
lib/platform/macos/      ← iOS inherits most of this (Darwin syscalls, CoreText, ...)
lib/platform/stubs/      ← Shared non-desktop stubs (WASM + iOS + future mobile/embedded)
lib/platform/ios/        ← Only the iOS-specific differences (5-6 files)
```

In CMake, the iOS target links the POSIX sources + macOS sources + iOS overrides. The iOS
files only provide implementations for symbols that need to differ from macOS.

#### Shared non-desktop stubs (`lib/platform/stubs/`)

These stubs apply to both WASM and iOS (and future mobile/embedded platforms). They handle
the "no real terminal, no shell, no PortAudio" reality of non-desktop targets. Currently
they live in `lib/platform/wasm/stubs/` — the plan is to promote the shared ones up to
`lib/platform/stubs/` and have both WASM and iOS CMake targets pull from there.

```
lib/platform/stubs/
├── terminal.c         # get_terminal_size from options, cursor hide/show no-ops
├── video.c            # image_validate_dimensions (range check, portable)
├── process.c          # popen/fork return ENOSYS (no shell on mobile/WASM)
├── portaudio.h        # Stub types (Pa_Initialize returns paNotInitialized)
├── audio.h            # Type definitions for audio_batch_info_t, audio_frame_t
├── actions.c          # action_list_webcams, action_create_manpage, etc. — no-ops
├── manpage.c          # get_manpage_template/content return empty (no man pages)
└── session_log_buffer.h  # Empty header (session log buffering handled by UI layer)
```

**What stays in `wasm/stubs/` (WASM-only)**:
- `crypto.c` — auto-accepts hosts, disables key verification. iOS needs REAL crypto.
- `filesystem.c` — routes fd 1/2 to `wasm_log_to_console()` via EM_JS. iOS uses real files.
- `log.c` — mmap stubs. iOS can use real mmap logging.

**What stays in `wasm/` (WASM-only)**:
- `threading.c` — no-op mutexes. iOS needs real pthreads.
- `terminal.c` — EM_JS bridges to xterm.js. iOS doesn't have JS.
- `console.c` — EM_JS to browser console. iOS uses os_log.

The WASM CMake target would change from `wasm/stubs/*` to `stubs/* + wasm/stubs/*` (shared + WASM-only).
The iOS CMake target would use `stubs/* + ios/*` (shared + iOS-only).

#### What works as-is from POSIX (no iOS code needed)

| Module               | Why it works                                         |
|----------------------|------------------------------------------------------|
| `threading.c`        | pthreads (mutex, rwlock, condvar, TLS) work on iOS   |
| `socket.c`           | BSD sockets work on iOS                              |
| `time.c`             | `clock_gettime`, `gettimeofday`, `nanosleep` all work|
| `string.c`           | Standard libc string functions                       |
| `memory.c`           | `malloc`/`free`/`mmap` work                          |
| `pipe.c`             | POSIX pipes work                                     |
| `errno.c`            | Standard errno                                       |
| `backtrace.c`        | Darwin backtrace works on iOS (Mach-O symbols)       |
| `lifecycle.c`        | Atomic operations and pthreads work                  |

#### What works from macOS (no or minor changes)

| Module               | Notes                                                |
|----------------------|------------------------------------------------------|
| `system.c`           | Darwin system calls, signal handling. `fork`/`exec` restricted but rarely called. |
| `font.c`             | CoreText works on iOS. Bundle paths may differ slightly. |
| `crypto/`            | Full libsodium crypto — works as-is on iOS arm64     |
| `webcam/macos/`      | Pure AVFoundation/CoreMedia/CoreVideo — rename to `webcam/apple/` and it works on iOS as-is. Zero macOS-only APIs. |

#### What iOS needs to override

Platform module (`lib/platform/ios/`) — only what the platform layer actually owns:

```
lib/platform/ios/
├── terminal.c             # No TTY — return options-based dimensions, truecolor
├── keepawake.m            # UIApplication.idleTimerDisabled (macOS uses IOKit)
├── filesystem.m           # Sandbox-aware paths (NSDocumentDirectory, app container)
├── process.c              # Stub popen/fork (restricted on iOS)
└── question.c             # Delegate prompts to Swift UI via registered callback
```

Files that live in their own modules, not in platform:

```
lib/log/ios_log.c                      # platform_log_hook() → os_log (#397)
lib/options/registry/ios_mode_defaults.c  # iOS defaults (truecolor, dimensions)
```

That's it. ~5 platform files + 2 module files vs. the WASM layer's ~22 files.

#### iOS override implementations

**terminal.c** — no TTY, but we know what the display supports:
```c
int get_terminal_size(unsigned short *rows, unsigned short *cols) {
    // Dimensions come from the Swift UI layer via options
    *cols = GET_OPTION(width) > 0 ? GET_OPTION(width) : 80;
    *rows = GET_OPTION(height) > 0 ? GET_OPTION(height) : 40;
    return 0;
}

term_color_support_t detect_color_support(void) {
    return TERM_COLOR_TRUECOLOR;  // iOS display supports full color
}

bool terminal_supports_utf8(void) { return true; }
bool is_output_piped(void) { return false; }
bool is_interactive_terminal(void) { return false; }

// Cursor control — no-ops (rendering handled by Swift view)
asciichat_error_t terminal_cursor_hide(void) { return ASCIICHAT_OK; }
asciichat_error_t terminal_cursor_show(void) { return ASCIICHAT_OK; }
```

**keepawake.m** — Objective-C for UIKit:
```objc
#import <UIKit/UIKit.h>

void platform_enable_keepawake(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIApplication sharedApplication].idleTimerDisabled = YES;
    });
}

void platform_disable_keepawake(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIApplication sharedApplication].idleTimerDisabled = NO;
    });
}
```

**filesystem.m** — sandbox-aware paths:
```objc
#import <Foundation/Foundation.h>

const char *platform_get_home_dir(void) {
    static char home[PATH_MAX];
    NSString *dir = NSHomeDirectory();
    strlcpy(home, [dir UTF8String], sizeof(home));
    return home;
}

const char *platform_get_config_dir(void) {
    static char config[PATH_MAX];
    NSArray *paths = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);
    strlcpy(config, [[paths firstObject] UTF8String], sizeof(config));
    return config;
}

const char *platform_get_data_dir(void) {
    static char data[PATH_MAX];
    NSArray *paths = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES);
    strlcpy(data, [[paths firstObject] UTF8String], sizeof(data));
    return data;
}

// Config file search: check app bundle + Documents
asciichat_error_t platform_find_config_file(const char *filename,
                                            char *result, size_t result_size) {
    // 1. Check app bundle
    NSString *bundlePath = [[NSBundle mainBundle] pathForResource:
        [NSString stringWithUTF8String:filename] ofType:nil];
    if (bundlePath) {
        strlcpy(result, [bundlePath UTF8String], result_size);
        return ASCIICHAT_OK;
    }
    // 2. Check Documents directory
    // ...
    return SET_ERRNO(ERROR_NOT_FOUND, "Config file not found: %s", filename);
}
```

**question.c** — delegate to Swift UI:
```c
// Register a callback from Swift that handles user prompts in the UI
typedef bool (*ios_prompt_callback_t)(const char *prompt, char *response, size_t max_len);
static ios_prompt_callback_t g_prompt_callback = NULL;

void ios_register_prompt_callback(ios_prompt_callback_t cb) {
    g_prompt_callback = cb;
}

bool platform_prompt_yes_no(const char *prompt, bool default_yes) {
    if (g_prompt_callback) {
        char response[8] = {0};
        if (g_prompt_callback(prompt, response, sizeof(response))) {
            return response[0] == 'y' || response[0] == 'Y';
        }
    }
    return default_yes;  // Fallback to default
}
```

**process.c** — stubs for restricted APIs:
```c
#include <errno.h>

// iOS doesn't support fork/exec/popen
int platform_popen(const char *command, const char *mode, FILE **result) {
    (void)command; (void)mode; (void)result;
    errno = ENOSYS;
    return -1;
}

// yt-dlp subprocess won't work on iOS — media URLs must be
// resolved by the Swift layer or pre-resolved before passing to library
```

**lib/log/ios_log.c** — route library logs to `os_log` ([#397](https://github.com/zfogg/ascii-chat/issues/397)):

Implements the `platform_log_hook()` weak symbol defined in `lib/log/log.c`.

```c
// lib/log/ios_log.c
#include <os/log.h>

static os_log_t ac_log = NULL;

void platform_log(int level, const char *msg) {
    if (!ac_log) ac_log = os_log_create("com.zfogg.ascii-chat", "lib");
    os_log_with_type(ac_log, level >= LOG_ERROR ? OS_LOG_TYPE_ERROR
                           : level >= LOG_WARN  ? OS_LOG_TYPE_DEFAULT
                           : OS_LOG_TYPE_DEBUG, "%{public}s", msg);
}
```

This makes all `log_debug()`, `log_info()`, `log_error()` etc. show up natively in the
unified logging system. Debug from a physical device without Xcode open:

```bash
# libimobiledevice — pipe device logs to terminal
brew install libimobiledevice
idevicesyslog | grep ascii-chat

# Or filter by process
idevicesyslog --process ascii-chat

# Simulator
xcrun simctl spawn booted log stream --predicate 'subsystem == "com.zfogg.ascii-chat"'

# Xcode's devicectl
xcrun devicectl device syslog --device <UDID> | grep ascii-chat
```

The existing `--grep` patterns work on the host side: `idevicesyslog | grep "/handshake/i"`.
Same debugging workflow, just over USB instead of in the same terminal.

**lib/options/registry/ios_mode_defaults.c** — iOS-appropriate defaults:

Lives alongside the existing `lib/options/registry/mode_defaults.c`.

```c
const char *get_default_log_file(void) {
    return NULL;  // Log to os_log, not file
}

int get_default_port(void) {
    return 27224;  // Same as desktop
}

void apply_mode_specific_defaults(void) {
    // iOS defaults: truecolor, UTF-8, reasonable dimensions
    if (GET_OPTION(color_mode) == 0)
        SET_OPTION(color_mode, TERM_COLOR_TRUECOLOR);
}
```

#### PortAudio on iOS

PortAudio uses CoreAudio underneath on macOS, but its CoreAudio host API is **macOS-specific**:
it uses AudioUnit directly without `AVAudioSession`, which iOS requires for audio session
management (categories, route switching, interruption handling, privacy permissions).
PortAudio has no official iOS support. Recompiling it for iOS won't work without significant
patches.

**Plan**: Stub PortAudio on iOS (same as WASM — `Pa_Initialize` returns `paNotInitialized`).
Handle audio I/O in Swift via `AVAudioEngine`, bridging PCM samples to the library's Opus
encoder/decoder. The library still owns the codec; Swift just owns the mic/speaker I/O and
the `AVAudioSession` lifecycle.

#### CMake integration

```cmake
# In lib/CMakeLists.txt or cmake/platform/

# Shared non-desktop stubs (used by both WASM and iOS)
set(PLATFORM_STUBS_SOURCES
    lib/platform/stubs/terminal.c
    lib/platform/stubs/video.c
    lib/platform/stubs/process.c
    lib/platform/stubs/actions.c
    lib/platform/stubs/manpage.c
)

if(PLATFORM_IOS)
    # POSIX + macOS base (iOS inherits these)
    list(APPEND PLATFORM_SOURCES
        lib/platform/posix/threading.c
        lib/platform/posix/socket.c
        lib/platform/posix/time.c
        lib/platform/posix/string.c
        lib/platform/posix/memory.c
        lib/platform/posix/pipe.c
        lib/platform/posix/errno.c
        lib/platform/posix/backtrace.c
        lib/platform/posix/lifecycle.c
        lib/platform/macos/system.c
        lib/platform/macos/font.c
        # ... other shared POSIX/macOS files
    )
    # Shared non-desktop stubs
    list(APPEND PLATFORM_SOURCES ${PLATFORM_STUBS_SOURCES})
    # iOS platform overrides
    list(APPEND PLATFORM_SOURCES
        lib/platform/ios/keepawake.m
        lib/platform/ios/filesystem.m
        lib/platform/ios/process.c
        lib/platform/ios/question.c
    )
    # iOS files in their own modules
    list(APPEND LOG_SOURCES lib/log/ios_log.c)
    list(APPEND OPTIONS_SOURCES lib/options/registry/ios_mode_defaults.c)
elseif(EMSCRIPTEN)
    # WASM gets shared stubs + its own stubs + its own implementations
    list(APPEND PLATFORM_SOURCES ${PLATFORM_STUBS_SOURCES})
    list(APPEND PLATFORM_SOURCES
        lib/platform/wasm/threading.c      # No-op mutexes (WASM-specific!)
        lib/platform/wasm/terminal.c       # EM_JS to xterm.js (WASM-specific!)
        lib/platform/wasm/console.c        # EM_JS to browser console
        lib/platform/wasm/system.c
        lib/platform/wasm/time.c
        lib/platform/wasm/string.c
        lib/platform/wasm/init.c
        lib/platform/wasm/stubs/crypto.c   # Auto-accept (WASM-specific!)
        lib/platform/wasm/stubs/filesystem.c
        lib/platform/wasm/stubs/log.c
    )
endif()
```

---

### 3. Swift ↔ C Bridge

#### 3a. Module map for Swift imports

Create a C module that Swift can import:

```
// AsciiChatLib/module.modulemap
module AsciiChatLib {
    header "ascii-chat-bridge.h"
    link "asciichat"
    export *
}
```

The bridge header exposes a clean, Swift-friendly subset of the C API:

```c
// ascii-chat-bridge.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

// -- Lifecycle --
int ac_init(const char *args_json);
void ac_cleanup(void);

// -- Frame I/O --
// Feed camera/file pixels into the library for processing
int ac_feed_frame(const uint8_t *rgba, int width, int height);

// -- Pixel Buffer Output (already exists: include/ascii-chat/media/render/renderer.h) --
// The terminal renderer (libvterm + FreeType) is already fully implemented.
// Create with term_renderer_create(), feed ANSI with term_renderer_feed(),
// read RGB24 pixels with term_renderer_pixels().
// See lib/media/render/terminal.c for implementation.

// -- Client Mode --
int ac_client_init(const char *args_json);
int ac_client_generate_keypair(void);
const char *ac_client_get_public_key_hex(void);
int ac_client_set_server_address(const char *host, int port);

// Crypto handshake (same pattern as WASM)
int ac_client_handle_key_exchange(const uint8_t *packet, size_t len);
int ac_client_handle_auth_challenge(const uint8_t *packet, size_t len);
int ac_client_handle_handshake_complete(const uint8_t *packet, size_t len);

// Encrypt/decrypt
int ac_client_encrypt(const uint8_t *plain, size_t plain_len,
                      uint8_t *cipher, size_t *cipher_len);
int ac_client_decrypt(const uint8_t *cipher, size_t cipher_len,
                      uint8_t *plain, size_t *plain_len);

// -- Audio (Opus) --
int ac_opus_encoder_init(int sample_rate, int channels, int bitrate);
int ac_opus_encode(const float *pcm, int frame_size,
                   uint8_t *out, int max_out);
int ac_opus_decoder_init(int sample_rate, int channels);
int ac_opus_decode(const uint8_t *data, int len,
                   float *pcm, int frame_size);

// -- Options --
void ac_set_option_int(const char *name, int value);
int ac_get_option_int(const char *name);
void ac_set_option_string(const char *name, const char *value);
const char *ac_get_option_string(const char *name);

// -- Callbacks --
typedef void (*ac_send_packet_fn)(const uint8_t *data, size_t len);
void ac_register_send_callback(ac_send_packet_fn fn);

typedef void (*ac_log_fn)(int level, const char *message);
void ac_register_log_callback(ac_log_fn fn);
```

This mirrors the WASM `EMSCRIPTEN_KEEPALIVE` functions almost exactly. The web client's `src/wasm/client.ts` and `src/wasm/mirror.ts` are the reference.

#### 3b. Swift wrapper class

```swift
import AsciiChatLib

@Observable
class AsciiChatEngine {
    var connectionState: ConnectionState = .disconnected
    var currentFrame: String = ""

    func initMirror(width: Int, height: Int) { ... }
    func feedFrame(_ rgba: Data, width: Int, height: Int) { ... }

    // Terminal renderer (wraps term_renderer_* from lib/media/render/)
    func createRenderer(cols: Int, rows: Int, font: String, fontSize: Float) { ... }
    func renderFrame(_ ansiText: String) { ... }  // term_renderer_feed
    func getPixels() -> (UnsafePointer<UInt8>, Int, Int, Int)? { ... }  // pixels, w, h, pitch

    func initClient(serverHost: String, port: Int) { ... }
    func handleIncomingPacket(_ data: Data) { ... }
    func sendVideoFrame(_ rgba: Data, width: Int, height: Int) { ... }

    // Options
    func setColorMode(_ mode: ColorMode) { ... }
    func setRenderMode(_ mode: RenderMode) { ... }
    func setPalette(_ chars: String) { ... }
    func setDimensions(width: Int, height: Int) { ... }
}
```

---

### 4. Swift App Structure

#### 4a. Project setup

```
src/ios/
├── AsciiChat.xcodeproj
├── AsciiChat/
│   ├── App.swift                    # @main, app entry
│   ├── ContentView.swift            # Tab view: Mirror / Client / Server / Discovery
│   ├── Engine/
│   │   ├── AsciiChatEngine.swift    # C bridge wrapper (@Observable)
│   │   ├── ConnectionManager.swift  # WebSocket/TCP + handshake orchestration
│   │   ├── PacketParser.swift       # ACIP packet framing & reassembly
│   │   └── AudioBridge.swift        # AVAudioEngine ↔ Opus bridge
│   ├── Views/
│   │   ├── FrameBufferView.swift    # Renders pixel buffer from library (RGBA → CGImage)
│   │   ├── CameraPreview.swift      # AVCaptureSession preview
│   │   ├── MirrorView.swift         # Local webcam → ASCII preview
│   │   ├── ClientView.swift         # Connect to server, render grid
│   │   ├── ServerView.swift         # Host a session
│   │   ├── DiscoveryView.swift      # Browse/join sessions via ACDS
│   │   └── SettingsView.swift       # Color mode, palette, render mode, etc.
│   ├── Camera/
│   │   ├── CameraManager.swift      # AVCaptureSession → RGBA pixel buffer
│   │   └── FrameThrottler.swift     # CADisplayLink-based frame rate control
│   ├── Network/
│   │   ├── WebSocketTransport.swift # URLSessionWebSocketTask wrapper
│   │   └── TCPTransport.swift       # NWConnection (Network.framework)
│   └── Resources/
│       └── Assets.xcassets
└── libasciichat.xcframework/        # Pre-built C library
```

#### 4b. Key Swift components

**CameraManager** - Frame capture:

```swift
class CameraManager: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    private let session = AVCaptureSession()
    private let queue = DispatchQueue(label: "camera", qos: .userInteractive)
    var onFrame: ((Data, Int, Int) -> Void)?  // RGBA, width, height

    func captureOutput(_ output: AVCaptureOutput,
                       didOutput sampleBuffer: CMSampleBuffer,
                       from connection: AVCaptureConnection) {
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        let rgba = convertToRGBA(pixelBuffer)
        onFrame?(rgba, width, height)
    }
}
```

**ConnectionManager** - Orchestrates the handshake (mirrors web `ClientConnection.ts`):

```swift
class ConnectionManager {
    private var transport: WebSocketTransport  // or TCPTransport
    private let engine: AsciiChatEngine

    func connect(host: String, port: Int) async throws {
        engine.initClient(serverHost: host, port: port)
        engine.generateKeypair()
        try await transport.connect(to: host, port: port)
        // Library sends CRYPTO_CLIENT_HELLO via registered callback
        // Then handle incoming packets in receive loop
    }

    func handlePacket(_ data: Data) {
        let type = PacketParser.peekType(data)
        switch type {
        case .cryptoKeyExchangeInit:
            engine.handleKeyExchange(data)
        case .cryptoAuthChallenge:
            engine.handleAuthChallenge(data)
        case .cryptoHandshakeComplete:
            engine.handleHandshakeComplete(data)
        case .encrypted:
            let decrypted = engine.decrypt(data)
            let inner = PacketParser.parse(decrypted)
            handleDecryptedPacket(inner)
        case .asciiFrame:
            let frame = PacketParser.parseAsciiFrame(data)
            // Update UI with frame.ansiString
        default:
            break
        }
    }
}
```

---

### 5. Display: Pixel Buffer Rendering

The library already has a complete terminal renderer that takes ANSI text and produces a
pixel buffer: `lib/media/render/terminal.c` (public API in
`include/ascii-chat/media/render/renderer.h`). It uses libvterm for terminal emulation and
FreeType for font rasterization. It's production code, already used for render-file mode.

The iOS app does **not** parse ANSI escape codes — it feeds ANSI text to the terminal
renderer and blits the resulting RGB24 pixel buffer to the screen. Same model as the WASM
web client rendering to a canvas.

The flow:

```
Library (C)                                          Swift App
───────────                                          ─────────
term_renderer_create(cols, rows, font, theme)
term_renderer_feed(renderer, ansi_frame, len)   →  pixels = term_renderer_pixels(renderer)
  libvterm parses ANSI → screen state                  width = term_renderer_width_px(renderer)
  FreeType renders glyphs → RGB24 buffer               height = term_renderer_height_px(renderer)
                                                       pitch = term_renderer_pitch(renderer)
                                                          ↓
                                                   CGImage / CALayer / Metal texture
```

The Swift side is trivial — just display an image that updates every frame:

```swift
struct FrameBufferView: View {
    let pixelData: UnsafePointer<UInt8>  // RGB24 from term_renderer_pixels()
    let width: Int
    let height: Int
    let pitch: Int  // from term_renderer_pitch()

    var body: some View {
        if let image = cgImageFromRGB(pixelData, width: width, height: height, pitch: pitch) {
            Image(decorative: image, scale: 1.0)
                .interpolation(.none)  // Crisp pixels, no smoothing
                .resizable()
                .aspectRatio(contentMode: .fit)
        }
    }
}

func cgImageFromRGB(_ data: UnsafePointer<UInt8>,
                     width: Int, height: Int, pitch: Int) -> CGImage? {
    let colorSpace = CGColorSpaceCreateDeviceRGB()
    let bitmapInfo = CGBitmapInfo(rawValue: CGImageAlphaInfo.none.rawValue)
    guard let provider = CGDataProvider(dataInfo: nil,
            data: data, size: pitch * height,
            releaseData: { _, _, _ in }) else { return nil }
    return CGImage(width: width, height: height,
                   bitsPerComponent: 8, bitsPerPixel: 24,
                   bytesPerRow: pitch, space: colorSpace,
                   bitmapInfo: bitmapInfo, provider: provider,
                   decode: nil, shouldInterpolate: false,
                   intent: .defaultIntent)
}
```

For better performance at high frame rates, use a `CALayer` or `MTKView` (Metal) instead
of SwiftUI `Image` — avoid re-creating CGImage every frame by reusing a
`CGContext` and just updating the backing data.

---

### 6. Audio Pipeline

```swift
class AudioBridge {
    private let audioEngine = AVAudioEngine()
    private let engine: AsciiChatEngine

    func startCapture() {
        let input = audioEngine.inputNode
        let format = AVAudioFormat(commonFormat: .pcmFormatFloat32,
                                   sampleRate: 48000, channels: 1,
                                   interleaved: false)!
        input.installTap(onBus: 0, bufferSize: 480, format: format) { buffer, _ in
            // Get PCM samples
            let pcm = buffer.floatChannelData![0]
            // Encode with library's Opus encoder
            let opus = self.engine.opusEncode(pcm, frameSize: 480)
            // Send via network
            self.connectionManager.sendAudio(opus)
        }
        try! audioEngine.start()
    }

    func playAudio(_ opusData: Data) {
        let pcm = engine.opusDecode(opusData, frameSize: 480)
        // Write to playback buffer / AVAudioPlayerNode
    }
}
```

---

### 7. Mode Support Matrix

| Mode              | iOS Support | Notes                                              |
|-------------------|-------------|-----------------------------------------------------|
| Mirror            | Full        | Camera → library → ASCII render → display           |
| Client            | Full        | Camera + network + crypto + render                  |
| Server            | Full        | Accept connections, mix video, stream ASCII          |
| Discovery         | Full        | Connect via session string through ACDS              |
| Discovery Service | **Skip**    | Not appropriate for mobile (runs as persistent daemon)|

---

### 8. iOS-Specific Considerations

#### App lifecycle

- **Background**: iOS suspends apps aggressively. For server mode, request background audio or location entitlement to stay alive (or accept suspension).
- **Interruptions**: Handle phone calls, notifications gracefully. Pause capture, maintain connection if possible.
- **Memory**: Monitor memory pressure. The ASCII rendering pipeline is lightweight but video frames can be large.

#### Network

- **Local network**: iOS requires `NSLocalNetworkUsageDescription` in Info.plist for LAN discovery (mDNS, direct TCP to local IPs).
- **WebSocket**: `URLSessionWebSocketTask` is built-in and works well. Handles TLS, reconnection.
- **TCP**: `NWConnection` from Network.framework. Modern, async, handles IPv4/IPv6.
- **NAT traversal**: UPnP/NAT-PMP from library should work (just UDP/TCP calls). May need `NSBonjourServices` for mDNS.

#### Camera

- **Privacy**: `NSCameraUsageDescription` and `NSMicrophoneUsageDescription` in Info.plist.
- **Orientation**: Handle device rotation. Feed correct orientation to library.
- **Front/back**: Support camera switching. Front camera should mirror horizontally (library already handles horizontal flip).

#### Battery

- **Frame throttle**: Don't render at 60fps if 15-20fps is fine. CADisplayLink with preferred frame rate.
- **Resolution**: Capture at lower resolution for ASCII art (320x240 is plenty for 80x24 terminal).
- **CPU**: ASCII conversion is CPU-bound. The library has SIMD paths (NEON on arm64) which help.

---

### 9. Third-Party Swift Libraries (Candidates)

| Library                | Purpose                        | Why                                    |
|------------------------|--------------------------------|----------------------------------------|
| swift-async-algorithms | Async sequences                | Clean async frame pipeline             |
| Network.framework      | TCP/UDP (built-in)             | Modern Apple networking                |
| AVFoundation (built-in)| Camera + audio                 | Standard iOS media APIs                |
| Combine (built-in)     | Reactive state                 | Settings changes → library updates     |

Intentionally minimal. The C library does the heavy lifting.

---

### 10. Implementation Phases

#### Phase 1: Library Compilation (1-2 weeks)

- [ ] Create `cmake/toolchains/iOS.cmake`
- [ ] Add `ios-device` and `ios-simulator` CMake presets
- [ ] Extend `PlatformDetection.cmake` with `PLATFORM_IOS`
- [ ] Create `lib/platform/ios/` directory with stubs
- [ ] Cross-compile dependencies (libsodium, FFmpeg, Opus, zstd, PCRE2, yyjson, libwebsockets)
- [ ] Build `libasciichat.a` for arm64 + arm64-simulator
- [ ] Package as `.xcframework`
- [ ] Verify: link into empty Xcode project, call `ac_init()` successfully

#### Phase 2: Mirror Mode (1 week)

- [ ] Xcode project setup with SwiftUI
- [ ] Module map for C library import
- [ ] `CameraManager` class (AVFoundation → RGBA)
- [ ] `AsciiChatEngine` Swift wrapper (init, feed_frame, get_framebuffer, options)
- [ ] `FrameBufferView` - display pixel buffer from library (CGImage or Metal)
- [ ] `MirrorView` - camera preview + rendered output side by side
- [ ] Frame throttling with CADisplayLink
- [ ] Settings UI: color mode, palette, render mode, dimensions

#### Phase 3: Client Mode (1-2 weeks)

- [ ] `WebSocketTransport` using URLSessionWebSocketTask
- [ ] `PacketParser` - ACIP packet framing and reassembly
- [ ] `ConnectionManager` - handshake state machine
- [ ] `ClientView` - connect UI, ASCII frame display
- [ ] Encryption/decryption through C bridge
- [ ] Handle reconnection (reinitialize crypto state)
- [ ] Audio capture and playback via `AudioBridge`

#### Phase 4: Server Mode (1 week)

- [ ] TCP listener via Network.framework (NWListener)
- [ ] Wire up library's server mode through bridge
- [ ] `ServerView` - status display, client list
- [ ] Handle multiple simultaneous clients
- [ ] Background execution strategy

#### Phase 5: Discovery Mode (1 week)

- [ ] ACDS client integration via library
- [ ] `DiscoveryView` - session browser, join by string
- [ ] Session creation UI for hosting
- [ ] WebSocket transport to ACDS server

#### Phase 6: Polish (ongoing)

- [ ] App icon, launch screen
- [ ] iPad layout (larger terminal, split view)
- [ ] External keyboard support
- [ ] Share sheet (share session string)
- [ ] TestFlight distribution
- [ ] Performance profiling and optimization
- [ ] Accessibility

---

### 11. Open Questions

1. **Server mode background** - How long can iOS keep a server alive in background? May need to document limitations.
2. **FFmpeg on iOS** - Full FFmpeg or just the codecs we need (libavcodec, libavformat, libavutil, libswscale)? Smaller is better for App Store.
3. **PortAudio replacement** - The library currently uses PortAudio everywhere. iOS stubs will no-op PortAudio, but we need the Swift `AudioBridge` to properly feed PCM to the library's Opus encoder. Need to define that bridge API.
4. **App Store review** - Server mode accepts inbound connections. Apple may have opinions. May need to document as "local network only" or similar.
5. **H.265 hardware encoding** - iOS has VideoToolbox for hardware H.265 encoding. Should we use that instead of FFmpeg's software encoder? Would dramatically reduce battery usage.
6. **FreeType on iOS** - The terminal renderer uses FreeType for font rasterization. Need to cross-compile FreeType for iOS (straightforward, widely done) and bundle a monospace font or use system fonts via CoreText as a fallback.

---

### 12. Build & Run (Target)

```bash
# Build library for iOS device
cmake --preset default -B build-ios -DBUILD_IOS=ON
cmake --build build-ios

# Build library for iOS simulator
cmake --preset default -B build-ios-sim -DBUILD_IOS_SIM=ON
cmake --build build-ios-sim

# Create XCFramework
xcodebuild -create-xcframework \
  -library build-ios/lib/libasciichat.a -headers include/ \
  -library build-ios-sim/lib/libasciichat.a -headers include/ \
  -output ios/libasciichat.xcframework

# Open Xcode project
open src/ios/AsciiChat.xcodeproj
```

---

*This is a living document. Updated as research continues and implementation progresses.*
