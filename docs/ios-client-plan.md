# ascii-chat iOS Client - Implementation Plan (WIP)

> **Status**: Draft / Research Phase
> **Last Updated**: 2026-03-05
> **Goal**: Native iOS app in Swift that uses libasciichat (compiled for iOS) to support server, client, mirror, and discovery modes.

---

## Overview

The iOS client runs the **actual C main functions** (`client_main`, `mirror_main`, `server_main`,
`discovery_main`) natively on iOS. All terminal output is captured into a buffer, rendered through
libvterm + FreeType into an RGB24 pixel buffer, and delivered to Swift via a registered callback
every frame. The Swift app is a thin display layer — it never reimplements session logic, handshake
state machines, packet parsing, or connection management. All of that stays in C where it already works.

This is the same philosophy as the WASM web client, but taken further: instead of reimplementing
the session orchestration in Swift by calling library functions, the C session code runs unmodified
and the iOS platform layer redirects its output to a pixel buffer callback.

**The Swift app is responsible for:**

- Displaying pixel buffers from the C render callback (CGImage/Metal)
- Providing camera frames via AVFoundation (the library's webcam code is shared macOS/iOS)
- Audio capture/playback via AVAudioEngine shim (replaces PortAudio symbols)
- Platform stubs where iOS differs from POSIX (terminal dimensions, filesystem, keepawake)
- UI chrome: mode selection, settings, session discovery browser

**Everything else runs in C** — ACIP protocol, crypto handshake, ASCII art conversion, color filters,
packet framing, WebSocket/TCP transport, options system, session lifecycle, render loop timing.

---

## Architecture

### Unified C Main → iOS Callback Architecture

The key insight: every byte of terminal output goes through `platform_write_all(STDOUT_FILENO, ...)`
in `lib/platform/system.c`. On iOS, we override this single function to feed bytes to libvterm
instead of writing to a file descriptor. libvterm + FreeType renders the terminal state to an
RGB24 pixel buffer, and a registered Swift callback receives it every frame.

```
┌─────────────────────────────────────────────────────────────┐
│                    Swift / SwiftUI App                      │
│  ┌──────────────┐  ┌───────────────┐  ┌──────────────────┐  │
│  │ FrameBuffer  │  │ Mode Select   │  │ Settings UI      │  │
│  │ View (pixel  │  │ + Session     │  │ (options pass    │  │
│  │  display)    │  │ Browser       │  │  through to C)   │  │
│  └──────▲───────┘  └───────┬───────┘  └────────┬─────────┘  │
│         │                  │                   │            │
│  ┌──────┴──────────────────▼───────────────────▼──────────┐ │
│  │     Swift ↔ C Bridge (module map + callbacks)          │ │
│  │  ios_register_frame_callback(swift_fn)                  │ │
│  │  asciichat_main(argc, argv) — runs on background thread│ │
│  └──────┬──────────────────┬───────────────────┬──────────┘ │
└─────────┼──────────────────┼───────────────────┼────────────┘
          │                  │                   │
┌─────────▼──────────────────▼───────────────────▼────────────┐
│                    libasciichat.a                           │
│                                                             │
│  client_main() / mirror_main() / server_main() /            │
│  discovery_main() — run unmodified                           │
│                                                             │
│  All ANSI output → platform_write_all(STDOUT_FILENO, ...)   │
│         │                                                   │
│         ▼                                                   │
│  iOS override: ios_output_feed(buf, len)                     │
│         │                                                   │
│         ▼                                                   │
│  vterm_input_write(vt, buf, len)    [libvterm parses ANSI]   │
│         │                                                   │
│         ▼                                                   │
│  term_renderer_feed() → RGB24 pixel buffer                   │
│         │                                                   │
│         ▼                                                   │
│  ios_frame_callback(pixels, w, h, pitch)  → Swift            │
│                                                             │
│  ┌─────────┐ ┌──────────┐ ┌────────┐ ┌─────────┐           │
│  │ Network │ │ Video /  │ │ Crypto │ │ Options │           │
│  │ TCP/WS  │ │ ASCII    │ │ Sodium │ │ System  │           │
│  │ ACIP    │ │ Render   │ │ E2E    │ │         │           │
│  └─────────┘ └──────────┘ └────────┘ └─────────┘           │
│  ┌─────────┐ ┌──────────┐ ┌────────┐ ┌─────────┐           │
│  │ Audio   │ │ Media    │ │ Discov │ │Platform │           │
│  │ Opus    │ │ FFmpeg   │ │ ACDS   │ │ iOS     │           │
│  └─────────┘ └──────────┘ └────────┘ └─────────┘           │
└─────────────────────────────────────────────────────────────┘
```

### How it compares

| Approach                         | iOS Client (Unified)                              |
|----------------------------------|---------------------------------------------------|
| Web client reimplements session  | iOS runs actual C session code unmodified          |
| Swift ConnectionManager          | **Eliminated** — C client_main handles connections |
| Swift PacketParser               | **Eliminated** — C ACIP code handles packets       |
| Swift handshake state machine    | **Eliminated** — C crypto handshake runs as-is     |
| Camera capture                   | Shared `webcam/apple/` (AVFoundation, macOS+iOS)   |
| Audio pipeline                   | AVAudioEngine shim replaces PortAudio symbols      |
| Display rendering                | libvterm + FreeType → pixel buffer → Swift callback |
| Timing / FPS                     | C render loop handles timing (CADisplayLink not needed) |
| React state / hooks (web)        | SwiftUI `@Observable` on frame callback            |

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
| PortAudio      | **Replace** - AVAudioEngine shim (`lib/audio/ios/portaudio_shim.m`) | 13 functions, same callback API         |

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
├── portaudio.h        # Stub types — WASM only (iOS uses AVAudioEngine shim instead)
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
├── system.c               # platform_write_all() override — routes STDOUT to render bridge
├── render_bridge.c        # libvterm + term_renderer + frame callback management
├── render_bridge.h        # Public API for registering callbacks
├── stdin_bridge.c         # Thread-safe queue: Swift pushes keystrokes, C reads them
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

That's ~8 platform files + 2 module files vs. the WASM layer's ~22 files.

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
#include "ascii-chat/error.h"

asciichat_error_t platform_enable_keepawake(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIApplication sharedApplication].idleTimerDisabled = YES;
    });
    return ASCIICHAT_OK;
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
#include "ascii-chat/error.h"
#include "ascii-chat/platform/filesystem.h"

const char *platform_get_home_dir(void) {
    static char home[PATH_MAX];
    NSString *dir = NSHomeDirectory();
    strlcpy(home, [dir UTF8String], sizeof(home));
    return home;
}

// Returns heap-allocated string (caller frees)
char *platform_get_config_dir(void) {
    NSArray *paths = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);
    const char *path = [[paths firstObject] UTF8String];
    size_t len = strlen(path) + 1;
    char *result = SAFE_MALLOC(len, char *);
    strlcpy(result, path, len);
    return result;
}

// Returns heap-allocated string (caller frees)
char *platform_get_data_dir(void) {
    NSArray *paths = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES);
    const char *path = [[paths firstObject] UTF8String];
    size_t len = strlen(path) + 1;
    char *result = SAFE_MALLOC(len, char *);
    strlcpy(result, path, len);
    return result;
}

// Config file search: check app bundle + Documents
asciichat_error_t platform_find_config_file(const char *filename,
                                            config_file_list_t *list_out) {
    // 1. Check app bundle
    NSString *bundlePath = [[NSBundle mainBundle] pathForResource:
        [NSString stringWithUTF8String:filename] ofType:nil];
    if (bundlePath) {
        // Add to list_out...
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
#include "ascii-chat/error.h"
#include "ascii-chat/platform/process.h"

// iOS doesn't support fork/exec/popen
asciichat_error_t platform_popen(const char *name, const char *command,
                                  const char *mode, FILE **out_stream) {
    (void)name; (void)command; (void)mode; (void)out_stream;
    return SET_ERRNO(ERROR_NOT_SUPPORTED,
                     "popen not available on iOS: %s", command);
}

// yt-dlp subprocess won't work on iOS — media URLs must be
// resolved by the Swift layer or pre-resolved before passing to library
```

**lib/log/ios_log.c** — route library logs to `os_log` ([#397](https://github.com/zfogg/ascii-chat/issues/397)):

Implements the `platform_log_hook()` weak symbol defined in `lib/log/log.c`.

```c
// lib/log/ios_log.c
// Overrides the weak symbol platform_log_hook() from lib/log/log.c
#include <os/log.h>
#include "ascii-chat/log/types.h"

static os_log_t g_os_log = NULL;

void platform_log_hook(log_level_t level, const char *message) {
    if (!g_os_log) g_os_log = os_log_create("com.zfogg.ascii-chat", "lib");
    os_log_with_type(g_os_log, level >= LOG_ERROR ? OS_LOG_TYPE_ERROR
                              : level >= LOG_WARN  ? OS_LOG_TYPE_DEFAULT
                              : OS_LOG_TYPE_DEBUG, "%{public}s", message);
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

void apply_ios_defaults(options_t *opts) {
    // iOS defaults: truecolor, UTF-8, reasonable dimensions.
    // Called during option parsing before options_state_set().
    if (opts->color_mode == 0)
        opts->color_mode = TERM_COLOR_TRUECOLOR;
}
```

#### PortAudio on iOS: AVAudioEngine shim

PortAudio itself can't be compiled for iOS (its CoreAudio host API is macOS-specific, missing
`AVAudioSession` integration). But instead of stubbing it out, we replace the PortAudio
symbols with real AVAudioEngine implementations so the library's entire audio pipeline —
callbacks, Opus encoding/decoding, mixing, AEC3 — works unchanged on iOS.

The library only uses 13 PortAudio functions:

```
Pa_Initialize          Pa_Terminate           Pa_GetErrorText
Pa_OpenStream          Pa_StartStream         Pa_StopStream
Pa_CloseStream         Pa_Sleep               Pa_WriteStream
Pa_GetDefaultInputDevice   Pa_GetDefaultOutputDevice
Pa_GetDeviceCount      Pa_GetDeviceInfo
```

And ~11 types/constants: `PaError`, `PaStream`, `PaDeviceIndex`, `PaDeviceInfo`,
`PaStreamParameters`, `PaStreamCallback`, `PaStreamCallbackTimeInfo`,
`PaStreamCallbackFlags`, `paFloat32`, `paClipOff`, `paNoError`, `paNoDevice`.

**Implementation**: `lib/audio/ios/portaudio_shim.m`

An Objective-C file that provides these symbols using AVAudioEngine:

```objc
// lib/audio/ios/portaudio_shim.m
#import <AVFoundation/AVFoundation.h>
#include "portaudio.h"  // Use the same header for type definitions

// Internal state per stream
typedef struct {
    AVAudioEngine *engine;
    PaStreamCallback *callback;
    void *user_data;
    bool is_input;
    bool is_output;
    bool is_running;
} pa_ios_stream_t;

PaError Pa_Initialize(void) {
    // Configure AVAudioSession for playback + recording
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;
    [session setCategory:AVAudioSessionCategoryPlayAndRecord
             withOptions:AVAudioSessionCategoryOptionDefaultToSpeaker |
                         AVAudioSessionCategoryOptionAllowBluetooth
                   error:&error];
    [session setActive:YES error:&error];
    return error ? paInternalError : paNoError;
}

PaError Pa_OpenStream(PaStream **stream,
                      const PaStreamParameters *inputParams,
                      const PaStreamParameters *outputParams,
                      double sampleRate, unsigned long framesPerBuffer,
                      PaStreamFlags flags, PaStreamCallback *callback,
                      void *userData) {
    pa_ios_stream_t *s = SAFE_CALLOC(1, sizeof(pa_ios_stream_t), pa_ios_stream_t *);
    s->engine = [[AVAudioEngine alloc] init];
    s->callback = callback;
    s->user_data = userData;
    s->is_input = (inputParams != NULL);
    s->is_output = (outputParams != NULL);

    if (s->is_input) {
        // Install tap on inputNode — delivers mic PCM to the PA callback
        AVAudioFormat *fmt = [[AVAudioFormat alloc]
            initWithCommonFormat:AVAudioPCMFormatFloat32
            sampleRate:sampleRate channels:1 interleaved:NO];
        [s->engine.inputNode installTapOnBus:0 bufferSize:(AVAudioFrameCount)framesPerBuffer
            format:fmt block:^(AVAudioPCMBuffer *buffer, AVAudioTime *when) {
            // Call the PortAudio callback with input data
            // ...
        }];
    }
    // Output: use AVAudioSourceNode to pull from PA callback
    // ...

    *stream = (PaStream *)s;
    return paNoError;
}

// Pa_StartStream, Pa_StopStream, Pa_CloseStream, etc. map to
// [engine startAndReturnError:], [engine stop], cleanup
```

This means:
- The library's `audio.c` code works **unchanged** on iOS
- Full duplex audio, Opus codec, AEC3 echo cancellation — all library-owned
- `AVAudioSession` lifecycle managed properly (categories, interruptions, Bluetooth)
- No Swift audio bridge needed — the `AudioBridge.swift` from the app structure is eliminated
- Device enumeration (`Pa_GetDeviceCount`/`Pa_GetDeviceInfo`) returns iOS audio routes

The shim lives alongside the library code, not in the Swift app. It links against
AVFoundation.framework and AudioToolbox.framework.

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

#### 3a. Render bridge (`lib/platform/ios/render_bridge.c`)

The render bridge is the core of the unified architecture. It manages the libvterm +
term_renderer instance and the registered Swift callback. It sits between
`platform_write_all()` and the Swift display layer.

```c
// lib/platform/ios/render_bridge.h
#pragma once

#include <stdint.h>
#include <stddef.h>

// Callback type: receives RGB24 pixel buffer every frame
typedef void (*ios_frame_callback_t)(const uint8_t *pixels,
                                     int width, int height,
                                     int pitch, void *ctx);

// Register the Swift callback that receives rendered frames
void ios_register_frame_callback(ios_frame_callback_t cb, void *ctx);

// Called by platform_write_all() when fd == STDOUT_FILENO
// Feeds bytes to libvterm, renders to pixels, calls callback
void ios_output_feed(const char *data, size_t len);

// Initialize the render bridge (creates libvterm + term_renderer)
// Called before any mode main function starts
void ios_render_bridge_init(int cols, int rows,
                            const char *font_path, double font_size);

// Teardown
void ios_render_bridge_destroy(void);

// Trigger a frame render (called at frame boundaries)
// The C render loop already has frame timing — this is called
// after each complete frame is written to stdout
void ios_render_bridge_flush(void);
```

```c
// lib/platform/ios/render_bridge.c
#include "render_bridge.h"
#include "ascii-chat/media/render/renderer.h"
#include <vterm.h>
#include <string.h>

static terminal_renderer_t *g_renderer = NULL;
static ios_frame_callback_t g_frame_cb = NULL;
static void *g_frame_ctx = NULL;

// Accumulation buffer for partial writes within a frame
static char *g_write_buf = NULL;
static size_t g_write_len = 0;
static size_t g_write_cap = 0;

void ios_register_frame_callback(ios_frame_callback_t cb, void *ctx) {
    g_frame_cb = cb;
    g_frame_ctx = ctx;
}

void ios_render_bridge_init(int cols, int rows,
                            const char *font_path, double font_size) {
    term_renderer_config_t cfg = {
        .cols = cols,
        .rows = rows,
        .font_size_pt = font_size,
        .theme = TERM_RENDERER_THEME_DARK,
        .font_is_path = true,
    };
    strlcpy(cfg.font_spec, font_path, sizeof(cfg.font_spec));
    term_renderer_create(&cfg, &g_renderer);

    // Pre-allocate write buffer
    g_write_cap = 64 * 1024;
    g_write_buf = SAFE_MALLOC(g_write_cap, char *);
    g_write_len = 0;
}

void ios_output_feed(const char *data, size_t len) {
    // Accumulate writes (a single frame may involve multiple write() calls)
    if (g_write_len + len > g_write_cap) {
        g_write_cap = (g_write_len + len) * 2;
        char *new_buf = SAFE_MALLOC(g_write_cap, char *);
        memcpy(new_buf, g_write_buf, g_write_len);
        SAFE_FREE(g_write_buf);
        g_write_buf = new_buf;
    }
    memcpy(g_write_buf + g_write_len, data, len);
    g_write_len += len;
}

void ios_render_bridge_flush(void) {
    if (!g_renderer || !g_frame_cb || g_write_len == 0) return;

    // Feed accumulated ANSI output to libvterm → pixel buffer
    term_renderer_feed(g_renderer, g_write_buf, g_write_len);
    g_write_len = 0;

    // Deliver pixels to Swift
    g_frame_cb(
        term_renderer_pixels(g_renderer),
        term_renderer_width_px(g_renderer),
        term_renderer_height_px(g_renderer),
        term_renderer_pitch(g_renderer),
        g_frame_ctx
    );
}

void ios_render_bridge_destroy(void) {
    if (g_renderer) term_renderer_destroy(g_renderer);
    g_renderer = NULL;
    SAFE_FREE(g_write_buf);
    g_write_buf = NULL;
}
```

#### 3b. platform_write_all() iOS override

In `lib/platform/ios/system.c`, override the write function to route stdout to the
render bridge:

```c
// lib/platform/ios/system.c
#include "render_bridge.h"
#include <unistd.h>

size_t platform_write_all(int fd, const void *buf, size_t count) {
    if (fd == STDOUT_FILENO) {
        // Route terminal output to libvterm render bridge
        ios_output_feed((const char *)buf, count);
        return count;
    }
    // All other FDs (stderr, sockets, files) use real write()
    // (reuse the POSIX implementation from lib/platform/posix/system.c)
    return platform_write_all_posix(fd, buf, count);
}
```

#### 3c. Frame boundary detection

The C render loop already has frame boundaries — `session_display_render_frame()` writes
a complete frame then flushes. We hook the flush to trigger the render bridge:

```c
// In the iOS terminal stub (lib/platform/ios/terminal.c):
asciichat_error_t terminal_flush(int fd) {
    if (fd == STDOUT_FILENO) {
        ios_render_bridge_flush();  // Render accumulated output → callback
    }
    return ASCIICHAT_OK;
}
```

This means every time the C code finishes writing a frame and calls `fflush(stdout)` or
`terminal_flush()`, the accumulated ANSI output is rendered to pixels and delivered to Swift.

#### 3d. Module map for Swift imports

```
// AsciiChatLib/module.modulemap
module AsciiChatLib {
    header "ascii-chat-bridge.h"
    link "asciichat"
    export *
}
```

The bridge header is now much simpler — just lifecycle + callbacks:

```c
// ascii-chat-bridge.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

// -- Render Bridge --
typedef void (*ios_frame_callback_t)(const uint8_t *pixels,
                                     int w, int h, int pitch, void *ctx);
void ios_register_frame_callback(ios_frame_callback_t cb, void *ctx);
void ios_render_bridge_init(int cols, int rows,
                            const char *font, double font_size);
void ios_render_bridge_destroy(void);

// -- Audio Bridge --
typedef void (*ios_audio_callback_t)(const float *samples,
                                     int frame_count, void *ctx);
void ios_register_audio_callback(ios_audio_callback_t cb, void *ctx);

// -- Prompt Bridge --
typedef bool (*ios_prompt_callback_t)(const char *prompt,
                                      char *response, size_t max_len);
void ios_register_prompt_callback(ios_prompt_callback_t cb);

// -- Lifecycle --
// NEW WRAPPER: Wraps main() from src/main.c — renamed so it doesn't
// conflict with Swift's @main entry point. Implementation is just:
//   int asciichat_main(int argc, char **argv) { return main(argc, argv); }
// Compiled only when BUILD_IOS is set. Blocks until mode exits.
// argv[0] = "ascii-chat", argv[1] = mode, argv[2..] = options
int asciichat_main(int argc, char **argv);

// EXISTING: from src/main.c — sets g_should_exit atomic flag.
// All mode loops check should_exit() and will exit cleanly.
void signal_exit(void);
bool should_exit(void);

// -- Stdin bridge (keyboard/touch input) --
// Push bytes into the stdin queue (read by the C code via platform_read)
void ios_stdin_push(const char *data, size_t len);

// -- Options (C code uses GET_OPTION(field) macro internally) --
// Options are set via argv passed to main(). At runtime, the C code
// accesses them via the RCU-based options system:
//   const options_t *opts = options_get();
//   opts->width, opts->height, opts->color_mode, etc.
// Swift sets options by passing CLI flags in argv to main().
```

#### 3e. Swift engine class

```swift
import AsciiChatLib

@Observable
class AsciiChatEngine {
    var frameImage: CGImage?
    var isRunning = false

    private var modeThread: Thread?

    func start(mode: String, options: [String] = []) {
        isRunning = true

        // Initialize render bridge with terminal dimensions
        let cols = options.intValue(for: "--width") ?? 80
        let rows = options.intValue(for: "--height") ?? 40
        ios_render_bridge_init(Int32(cols), Int32(rows),
                               fontPath, fontSize)

        // Register pixel callback
        let ctx = Unmanaged.passUnretained(self).toOpaque()
        ios_register_frame_callback({ pixels, w, h, pitch, ctx in
            guard let ctx = ctx else { return }
            let engine = Unmanaged<AsciiChatEngine>.fromOpaque(ctx)
                .takeUnretainedValue()
            let image = engine.cgImageFromRGB(pixels!, width: Int(w),
                                              height: Int(h), pitch: Int(pitch))
            DispatchQueue.main.async {
                engine.frameImage = image
            }
        }, ctx)

        // Build argv: ["ascii-chat", mode, ...options]
        var argv = ["ascii-chat", mode] + options

        // Run the actual C main on a background thread
        DispatchQueue.global(qos: .userInteractive).async {
            argv.withCStringArray { cArgs in
                asciichat_main(Int32(cArgs.count), cArgs)
            }
            DispatchQueue.main.async {
                self.isRunning = false
            }
        }
    }

    func stop() {
        signal_exit()
    }

    // Send keyboard input to the C code
    func sendKey(_ key: String) {
        key.withCString { ios_stdin_push($0, key.utf8.count) }
    }
}
```

No ConnectionManager. No PacketParser. No handshake state machine. The C code
handles all of that — the Swift side just displays pixels and sends keystrokes.

---

### 4. Swift App Structure

#### 4a. Project setup

The app structure is dramatically simpler because the C code handles session lifecycle,
networking, handshakes, and packet parsing. Swift only does: UI chrome, pixel display,
and input forwarding.

```
src/ios/
├── AsciiChat.xcodeproj
├── AsciiChat/
│   ├── App.swift                    # @main, app entry
│   ├── ContentView.swift            # Tab view: Mirror / Client / Server / Discovery
│   ├── Engine/
│   │   └── AsciiChatEngine.swift   # C bridge wrapper (@Observable) — starts/stops modes
│   ├── Views/
│   │   ├── FrameBufferView.swift    # Renders pixel buffer from callback (RGB24 → CGImage)
│   │   ├── ModeView.swift           # Shared view for all modes (pixel display + input)
│   │   ├── MirrorView.swift         # Mirror mode config + ModeView
│   │   ├── ClientView.swift         # Client mode config (host/port) + ModeView
│   │   ├── ServerView.swift         # Server mode config + ModeView
│   │   ├── DiscoveryView.swift      # Session browser + ModeView
│   │   └── SettingsView.swift       # Color mode, palette, render mode, dimensions
│   ├── Input/
│   │   ├── KeyboardHandler.swift   # On-screen keyboard → ios_stdin_push()
│   │   └── GestureHandler.swift    # Tap/swipe → key sequences (q, ?, arrows)
│   └── Resources/
│       └── Assets.xcassets
└── libasciichat.xcframework/        # Pre-built C library
```

**What's gone** (handled by C code running natively):
- ~~`ConnectionManager.swift`~~ — C `client_main()` handles connections
- ~~`PacketParser.swift`~~ — C ACIP code handles packets
- ~~`CameraManager.swift`~~ — C webcam code (`webcam/apple/`) handles capture
- ~~`FrameThrottler.swift`~~ — C render loop handles timing
- ~~`Network/WebSocketTransport.swift`~~ — C libwebsockets handles WebSocket
- ~~`Network/TCPTransport.swift`~~ — C TCP code handles connections

#### 4b. Key Swift components

**ModeView** — The shared display view for all modes. Since every mode produces
terminal output that gets rendered to pixels, one view handles them all:

```swift
struct ModeView: View {
    @ObservedObject var engine: AsciiChatEngine

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            if let image = engine.frameImage {
                Image(decorative: image, scale: 1.0)
                    .interpolation(.none)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
            }
        }
        .onTapGesture { engine.sendKey("?") }  // Toggle help
        .gesture(
            DragGesture(minimumDistance: 50)
                .onEnded { value in
                    // Swipe down = quit
                    if value.translation.height > 100 { engine.stop() }
                }
        )
    }
}
```

**ClientView** — just a config form that starts the C client_main:

```swift
struct ClientView: View {
    @ObservedObject var engine: AsciiChatEngine
    @State private var host = "localhost"
    @State private var port = "27224"
    @State private var password = ""

    var body: some View {
        if engine.isRunning {
            ModeView(engine: engine)
        } else {
            Form {
                TextField("Host", text: $host)
                TextField("Port", text: $port)
                SecureField("Password (optional)", text: $password)
                Button("Connect") {
                    var opts = ["--address", host, "--port", port]
                    if !password.isEmpty { opts += ["--password", password] }
                    engine.start(mode: "client", options: opts)
                }
            }
        }
    }
}
```

Same pattern for MirrorView, ServerView, DiscoveryView — just different option forms.

---

### 5. stdout Capture → libvterm → Callback Pipeline

This is the core innovation of the iOS architecture. Instead of reimplementing session
logic in Swift, we intercept all terminal output at the platform layer.

#### How terminal output flows today (desktop)

```
session_display_convert_to_ascii()     # Image → ANSI escape codes
    ↓
session_display_render_frame()         # Adds cursor control, frame boundary
    ↓
session_display_write_ascii()          # Chooses write path based on TTY mode
    ↓
platform_write_all(STDOUT_FILENO, buf, len)    # Single interception point
    ↓
write() syscall → terminal emulator → user sees ASCII art
```

Every mode's output goes through `platform_write_all(STDOUT_FILENO, ...)`:
- **Mirror**: webcam → ASCII frames → stdout
- **Client**: received network frames → ASCII → stdout; status screens → stdout
- **Server**: status screen with live logs → stdout
- **Discovery**: status/session UI → stdout

The session screens (splash, status) also go through this path via the `frame_buffer`
abstraction, which ultimately calls `platform_write_all(g_terminal_screen_output_fd, ...)`.

#### How it flows on iOS

```
session_display_convert_to_ascii()     # Same as desktop
    ↓
session_display_render_frame()         # Same as desktop
    ↓
session_display_write_ascii()          # Same as desktop
    ↓
platform_write_all(STDOUT_FILENO, buf, len)    # iOS OVERRIDE
    ↓
ios_output_feed(buf, len)              # Accumulates in write buffer
    ↓
[frame boundary: terminal_flush() or fflush(stdout)]
    ↓
ios_render_bridge_flush()
    ↓
term_renderer_feed(renderer, accumulated_buf, len)   # libvterm parses ANSI
    ↓
FreeType renders glyphs → RGB24 pixel buffer
    ↓
ios_frame_callback(pixels, w, h, pitch, ctx)  → Swift UI update
```

Zero changes to the C render pipeline. The iOS platform layer just provides a different
`platform_write_all()` that feeds libvterm instead of calling `write()`.

#### Why this works

1. **Single interception point**: All output goes through `platform_write_all()`. One function override captures everything.
2. **Frame boundaries are explicit**: The C code already calls `fflush(stdout)` / `terminal_flush()` after each complete frame. We hook this to trigger rendering.
3. **libvterm handles all state**: Cursor positioning, scrolling, colors, attributes — libvterm maintains full terminal state. We don't need to track anything.
4. **term_renderer is production code**: Already used for render-file mode. Tested, handles all ANSI escape sequences, produces clean RGB24 output.
5. **Buffering is already disabled**: `setvbuf(stdout, NULL, _IONBF, 0)` ensures writes happen immediately. No surprises from libc buffering.

---

### 6. Display: Pixel Buffer Rendering

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

### 7. Audio Pipeline

Audio is handled entirely by the library via the PortAudio → AVAudioEngine shim
(`lib/audio/ios/portaudio_shim.m`). No Swift audio code needed. The library's full audio
pipeline works unchanged: mic capture, Opus encoding, network transport, Opus decoding,
speaker playback, AEC3 echo cancellation, and audio mixing.

---

### 8. Mode Support Matrix

| Mode              | iOS Support | C Function        | What Displays                    |
|-------------------|-------------|--------------------|---------------------------------|
| Mirror            | Full        | `mirror_main()`   | Webcam → ASCII art → pixels     |
| Client            | Full        | `client_main()`   | Network frames → ASCII → pixels |
| Server            | Full        | `server_main()`   | Status screen + logs → pixels   |
| Discovery         | Full        | `discovery_main()` | Session UI + ASCII → pixels     |
| Discovery Service | **Skip**    | `acds_main()`     | Not appropriate for mobile      |

All modes produce terminal output that goes through the same stdout capture → libvterm → pixel
callback pipeline. The Swift app doesn't need mode-specific rendering logic.

---

### 9. iOS-Specific Considerations

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

### 10. Third-Party Swift Libraries (Candidates)

| Library                | Purpose                        | Why                                    |
|------------------------|--------------------------------|----------------------------------------|
| swift-async-algorithms | Async sequences                | Clean async frame pipeline             |
| Network.framework      | TCP/UDP (built-in)             | Modern Apple networking                |
| AVFoundation (built-in)| Camera + audio                 | Standard iOS media APIs                |
| Combine (built-in)     | Reactive state                 | Settings changes → library updates     |

Intentionally minimal. The C library does the heavy lifting.

---

### 11. Implementation Phases

#### Phase 1: Library Compilation + iOS Platform Layer (1-2 weeks)

- [ ] Create `cmake/toolchains/iOS.cmake`
- [ ] Add `BUILD_IOS` and `BUILD_IOS_SIM` CMake options
- [ ] Extend `PlatformDetection.cmake` with `PLATFORM_IOS`
- [ ] Create `lib/platform/ios/` directory with overrides:
    - [ ] `system.c` — `platform_write_all()` routes STDOUT to render bridge
    - [ ] `render_bridge.c` — libvterm + term_renderer + frame callback
    - [ ] `terminal.c` — dimensions from options, truecolor, no TTY
    - [ ] `keepawake.m` — UIApplication.idleTimerDisabled
    - [ ] `filesystem.m` — sandbox-aware paths
    - [ ] `process.c` — stub popen/fork
    - [ ] `question.c` — delegate prompts to Swift callback
    - [ ] `stdin_bridge.c` — queue for keyboard input from Swift
- [ ] Create module files:
    - [ ] `lib/log/ios_log.c` — os_log integration
    - [ ] `lib/options/registry/ios_mode_defaults.c`
- [ ] Rename `lib/video/webcam/macos/` → `lib/video/webcam/apple/`
- [ ] Write PortAudio → AVAudioEngine shim (`lib/audio/ios/portaudio_shim.m`)
- [ ] Cross-compile dependencies (libsodium, FFmpeg, Opus, zstd, PCRE2, yyjson, libwebsockets, libvterm, FreeType)
- [ ] Build `libasciichat.a` for arm64 + arm64-simulator
- [ ] Package as `.xcframework`
- [ ] Verify: link into empty Xcode project, call `asciichat_main()` with mirror mode

#### Phase 2: Mirror Mode — First Pixels (1 week)

This is the "hello world" milestone: the actual C `mirror_main()` runs on iOS and
produces pixels on screen.

- [ ] Xcode project setup with SwiftUI in `src/ios/`
- [ ] Module map for C library import
- [ ] `AsciiChatEngine.swift` — start/stop modes, frame callback → CGImage
- [ ] `FrameBufferView.swift` — display CGImage from callback
- [ ] `ModeView.swift` — shared pixel display with gesture input
- [ ] `MirrorView.swift` — options form + ModeView
- [ ] Verify: mirror mode shows ASCII webcam output on device
- [ ] Settings UI: color mode, palette, render mode, dimensions (passed as argv)

#### Phase 3: Client + Server + Discovery Modes (1-2 weeks)

Because the C code runs unmodified, each mode is just a new options form that
calls `asciichat_main()` with different argv. The display is the same ModeView.

- [ ] `ClientView.swift` — host/port/password form → starts client_main
- [ ] `ServerView.swift` — port/max-clients form → starts server_main
- [ ] `DiscoveryView.swift` — session string entry → starts discovery_main
- [ ] `KeyboardHandler.swift` — on-screen keyboard → ios_stdin_push()
- [ ] `GestureHandler.swift` — tap/swipe → key sequences
- [ ] Verify: connect iOS client to desktop server, see ASCII video
- [ ] Verify: run server on iOS, connect desktop client
- [ ] Verify: discovery mode works through ACDS

#### Phase 4: Polish (ongoing)

- [ ] App icon, launch screen
- [ ] iPad layout (larger terminal, split view)
- [ ] External keyboard support (physical keyboards send to stdin bridge)
- [ ] Share sheet (share session string)
- [ ] TestFlight distribution
- [ ] Performance profiling: CGImage vs CALayer vs Metal for pixel display
- [ ] Battery optimization: ensure render loop FPS matches C-side frame rate
- [ ] Accessibility
- [ ] Handle iOS audio interruptions in AVAudioEngine shim (phone calls, etc.)

---

### 12. Open Questions

1. **Frame boundary detection** - The current approach hooks `terminal_flush()` to trigger
   render bridge flush. Need to verify this fires exactly once per frame in all modes
   (mirror, client, server status, discovery status). If not, may need an explicit
   `ios_render_bridge_flush()` call in `session_display_render_frame()` behind `#ifdef PLATFORM_IOS`.
2. **Stdin bridge threading** - The C code reads stdin for keystrokes on a blocking thread.
   The stdin bridge needs a thread-safe queue (condition variable + ring buffer) that blocks
   `platform_read(STDIN_FILENO)` until Swift pushes data. Simple but needs to be right.
3. **Server mode background** - How long can iOS keep a server alive in background? May need
   to document limitations. Background audio entitlement keeps the process alive but requires
   actual audio playback.
4. **FFmpeg on iOS** - Full FFmpeg or just the codecs we need (libavcodec, libavformat,
   libavutil, libswscale)? Smaller is better for App Store.
5. **PortAudio shim completeness** - The AVAudioEngine shim needs to handle iOS audio
   interruptions (phone calls), route changes (Bluetooth connect/disconnect), and
   `AVAudioSession` reactivation. These have no PortAudio equivalent — may need a small
   iOS-specific hook in the library's audio init path.
6. **App Store review** - Server mode accepts inbound connections. Apple may have opinions.
   May need to document as "local network only" or similar.
7. **H.265 hardware encoding** - iOS has VideoToolbox for hardware H.265 encoding. Should we
   use that instead of FFmpeg's software encoder? Would dramatically reduce battery usage.
8. **FreeType on iOS** - The terminal renderer uses FreeType for font rasterization. Need to
   cross-compile FreeType for iOS (straightforward, widely done) and bundle a monospace font
   or use system fonts via CoreText as a fallback.
9. **Pixel buffer copy cost** - The frame callback fires from the C render thread. Swift needs
   to copy the pixel data before `term_renderer_feed()` overwrites it on the next frame.
   At 80x40 with a ~12pt font that's roughly 640x640 pixels × 3 bytes = ~1.2MB per frame.
   At 30fps that's ~36MB/s of copies. Probably fine, but profile it. Could use double-buffering
   in the render bridge to avoid the copy.
10. **Multiple modes** - Can we run two modes simultaneously (e.g., mirror + discovery)?
   Currently `asciichat_main()` is blocking and uses global state (`g_should_exit`). Probably
   one mode at a time for v1.

---

### 13. Build & Run (Target)

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
