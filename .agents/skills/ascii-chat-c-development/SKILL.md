---
name: ascii-chat-c-development
description: Develop and debug ascii-chat's native C code across transports, media, rendering, TUIs, crypto, and WASM. Use for implementation, debugging, or review in this repository.
metadata:
  short-description: ascii-chat C development and debugging
---

# ascii-chat C development

Use the repository's abstractions and pipelines. Before changing behavior, find the existing API in `include/ascii-chat/`, its implementation in `lib/`, and the mode/pipeline caller in `src/`; extend one path instead of creating protocol-specific or mode-specific duplicates.

## Errors and failure handling

- Functions that can fail return `asciichat_error_t`, not `int`; invalid parameters are errors too. Use `SET_ERRNO(ERROR_..., "context: %s", ...)` and propagate the result.
- Do not log a warning and continue with invalid or partial state. Handle the error, return nonzero, terminate the failing thread, or use the project's panic/exit path so the failure is resolved.
- The errno context is a stack and captures a backtrace. When a fallback is intentional and succeeds, clear/consume the expected error before returning success; do not leave stale errno state.
- Use the logging system, never `printf`/`fprintf` for diagnostics. UI-facing errors go through the TUI notice/error-popup path; the popup can display the saved error and its backtrace. Live TUIs include splash, status/discovery status, help, and error/notice screens; press `?` for interactive keyboard help.

## Backtraces and synchronization debugging

Use `debug/backtrace.h` APIs (`backtrace_capture`, `backtrace_symbolize`, `backtrace_print`, `backtrace_format`) rather than ad-hoc platform code. Debugging support is build-dependent; line/function quality depends on symbols, frame pointers, dSYM/PDB/debug info, and the platform symbolizer. WASM uses stubs and does not provide native traces.

Errors, crash handlers, explicit `--backtrace`, and selected debug paths capture traces automatically. Symbolization is comparatively expensive and may batch/launch `llvm-symbolizer` or equivalent, so never capture or symbolize once per audio/video frame or inside a realtime render loop. Capture at lifecycle boundaries, failures, or a sampled/debug-only path; symbolize when presenting the diagnostic.

For hangs, use `--sync-state` and the sync-debug APIs, especially delayed collection such as `debug_sync_print_backtrace_delayed(...)`, to inspect mutexes, conditions, threads, and lock owners without immediately perturbing the deadlock. On supported non-Windows debug builds, `SIGUSR1` triggers synchronization state, errno-stack, and diagnostic-stat reports; `SIGUSR2` requests a deferred memory report handled by the diagnostics worker. Then inspect the emitted logs/TUI. Reproduce with `--log-file`, `--grep`, and an appropriate non-release debug build.

## Transports, crypto, and media

- Network C code uses abstract transport APIs and the shared packet/session pipelines. Think in transports (TCP, WebSocket, WebRTC/DTLS, etc.), not duplicated implementations per protocol.
- The custom encrypted session uses ephemeral X25519-style key exchange and authenticated encryption; password and SSH/Ed25519 identity modes add authentication/TOFU. HTTPS/TLS belongs to the platform/network transport and key-fetch paths; DTLS is provided by the WebRTC transport. Reuse the existing handshake and transport layers.
- Media flows through FFmpeg and shared decoder/encoder pipelines. Raw pixel frames are internal buffers; H.264/H.265 use FFmpeg codec contexts (including `libx264` where available), and rendering consumes decoded frames rather than inventing a second conversion path. Check the active build's FFmpeg codecs and OS support before promising a format.
- Sources include webcams, microphones, speakers, local files, HTTP/HTTPS/RTSP/HLS and other FFmpeg inputs; yt-dlp resolves supported complex sites before FFmpeg. Preserve capability probing, defaults, resolution/FPS/sample-rate negotiation, and error propagation.

## ASCII rendering

The renderer converts owned pixel/image buffers into terminal cells at high FPS using scalar or SIMD implementations. Preserve buffer ownership, dimensions/stride, palette and capability negotiation, and row-run/RLE output optimizations. The two text renderer choices are the luminance and glyph renderers; inspect `options.h` and renderer code for current defaults/palettes rather than hard-coding them. Keep hot loops allocation-free and free of logging, symbolization, or blocking I/O.

## WASM

`src/web` and `lib/platform/wasm` provide browser bindings and stubs. Do not add native/OS dependencies to WASM unless explicitly requested. Guard non-WASM includes and code with the existing build conditionals and add/update a stub when the API must exist in the browser build.

## Build and review habits

On Windows, use the native PowerShell environment, Scoop/LLVM Clang, Ninja, and the `default` preset. Reconfigure with `cmake --preset default -B build` and build with `cmake --build build`; do not pass explicit parallelism flags such as `-j` or `--parallel`—let Ninja use all available cores. Do not substitute MSVC or GCC. On Linux/macOS, use the cross-platform skill for the prescribed Docker or SSH environment. Required dependencies must be required and linked where symbols are used; conditionals must represent real platform/feature variants. Use `SAFE_MALLOC/CALLOC/FREE`, platform abstractions, and documented ownership. Check Doxygen comments and generated interfaces when changing public headers. Run the narrowest relevant build/tests, then broader tests when the change crosses pipelines or platforms. Use recent git history as context for recurring failure areas: CMake/dependency selection, webcam/media capability probing, synchronization, renderer performance, and cross-platform/WASM boundaries.

For test selection, Criterion/JUnit/coverage, or test diagnosis, also load [`ascii-chat-testing`](../ascii-chat-testing/SKILL.md). For Linux Docker or macOS SSH builds, compiler selection, cache preservation, or cross-platform artifact inspection, also load [`ascii-chat-cross-platform-development-and-or-testing`](../ascii-chat-cross-platform-development-and-or-testing/SKILL.md).

When the task is complete, the code is merged into the main worktree, and the changes are pushed, remove obsolete build directories and temporary git worktrees created during the task. Verify the target paths first; preserve the main worktree, its active `build/`, uncommitted user work, and any worktree still needed by another task.

