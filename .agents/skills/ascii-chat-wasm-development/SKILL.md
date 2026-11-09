---
name: ascii-chat-wasm-development
description: Develop, debug, review, and build ascii-chat's Emscripten/WASM browser modules, Embind APIs, shared web renderer, and browser transports. Use when work crosses src/web, lib/platform/wasm, web/packages/shared, web/web, or the CMake WASM targets.
metadata:
  short-description: ascii-chat browser WASM development
---

# ascii-chat WASM development

ascii-chat started life as a native CLI. The browser version works because we
carefully reuse the CLI's real media, crypto, protocol, and rendering code and
put a thin browser-shaped surface around it. Keep that story intact: make the
smallest platform adaptation necessary, preserve the shared pipeline, and do
not invent a browser-only implementation when the native implementation can be
made portable.

## Start by orienting yourself

Before changing code, inspect the current tree and run `git status --short
--branch`. Existing edits are the user's. The important places are:

- `src/web/`: the nested Emscripten project. `CMakeLists.txt` selects the
  browser source set, WASM platform files/stubs, shared native libraries,
  generated fonts, bindings, and the `client`, `mirror`, `renderer`, and host
  entry points. `src/web/main_wasm.c`, `mirror.c`, `client.c`, `renderer/`, and
  `bindings/` are the first places to read for a module change.
- `src/web/bindings/`: Embind translation units. These are the deliberate
  JavaScript ABI, not a place to expose every C function. Generated TypeScript
  declarations live separately under `web/packages/shared/src/wasm/generated/`.
- `lib/platform/wasm/`: browser implementations of platform services and
  stubs. `stubs/` supplies APIs that make sense for native CLI code but not in
  a browser. `ffmpeg_decoder.c` and `lib/video/webcam/wasm/webcam_wasm.c` are
  examples of browser-specific media/platform paths.
- `include/` and `lib/`: the shared public headers and reusable implementation.
  Prefer changing a shared library or its platform abstraction over copying a
  native feature into `src/web`.
- `src/common/` and `src/common/session/`: shared session, frame, option, and
  lifecycle orchestration used by native and browser builds. Browser entry
  points intentionally avoid terminal `main()` loops and let TypeScript drive
  the render/event loop.
- `cmake/`: authoritative build machinery. Read `cmake/targets/Wasm.cmake`,
  `cmake/scripts/build_wasm.cmake`, `cmake/toolchains/Emscripten.cmake`,
  `cmake/init/Options.cmake`, `cmake/deps/*.cmake`, and `cmake/targets/Fonts.cmake`.
- `web/packages/shared/`: reusable TypeScript/React-facing browser package.
  `src/wasm/options.ts` is the shared, typed options contract; `src/wasm/`
  contains module loaders and generated binding types; `src/components/AsciiRenderer/`
  contains the renderer component, hooks, workers, and raster helpers.
- `web/web/`: the `web.ascii-chat.com` application. `src/wasm/` loads the
  module artifacts; `src/network/` owns WebRTC, WebTransport/QUIC, WSS/fallback,
  ACIP packet handling, browser video encoding/decoding, discovery, handoff,
  and session security; `src/hooks/` and `src/components/` connect those APIs
  to the site.
- `web/web/src/wasm/dist/`: canonical checked-in WASM artifacts for the web
  package (`*.js`, `*.wasm`, and the manifest). Runtime copies under
  `web/web/public/wasm/` are generated/ignored; do not hand-edit them.
- `deps/ascii-chat-deps/`: vendored dependencies, notably yyjson. WASM
  dependency build/cache configuration is in `cmake/deps/` and uses the shared
  `.deps-cache/` rather than an ad-hoc cache.
- `build/web/`: the nested Emscripten build directory created by the parent
  CMake project. Do not delete it to "fix" a build; reconfigure it.

## What the four targets mean

The parent project exposes four custom targets from `cmake/targets/Wasm.cmake`:

- `client-web`: browser client/session/crypto/network API used by the web site
  to interoperate with CLI and iOS clients.
- `web`: the convenience target that builds `mirror-web` and `client-web`.
- `mirror-web`: browser camera/file/test-pattern/audio/mirror processing used
  by `web.ascii-chat.com`.
- `renderer-web`: the renderer WASM package published with `options.ts`, the
  `AsciiRenderer` component, and its hooks/utilities. It is the reusable way
  to render ASCII art in another web application.

`client-web`, `web`, and `mirror-web` serve the web.ascii-chat.com product and
its browser-facing service APIs. `renderer-web` is the separately reusable
renderer surface. The site is deliberately built on these APIs so a browser
can use much of ascii-chat's real feature set while remaining compatible with
native CLI and iOS peers.

`BUILD_WEB=OFF` is the default. The normal native build therefore does not
require Emscripten or browser dependencies. Enable it through CMake and let
CMake configure the nested Emscripten project. Do not invoke `emcmake` or
`emmake` manually; this repository's CMake files select the toolchain and run
the nested configure/build internally.

Typical commands are:

```powershell
cmake --preset default -B build
cmake --build build
cmake --preset default -B build -DBUILD_WEB=ON
cmake --build build --target client-web
cmake --build build --target web
cmake --build build --target mirror-web
cmake --build build --target renderer-web
```

Use the current preset and `cmake --build` output as the authority if these
examples drift. Release web artifacts are normally built from the repository's
web deployment workflow and published by `web/web/scripts/wasm-build.mjs`.

## Making CLI C work in a browser

WASM is not a second application. It is the CLI's reusable C machinery with
the terminal, process, filesystem, native sockets, OS threads, signals, and
blocking entry loops replaced or removed. Use existing guards and platform
abstractions:

```c
#ifndef __EMSCRIPTEN__
  /* native process, terminal, or OS-only behavior */
#endif

#ifdef __EMSCRIPTEN__
  /* browser scheduling or platform behavior */
#endif
```

Examples in this tree include `lib/media/yt_dlp.c` excluding subprocess
extraction, `lib/options/colorscheme.c` and `lib/options/parsing/parsers.c`
excluding native-only behavior, `lib/network/tcp/transport.c` excluding
native socket paths, `src/common/session/host.c` excluding blocking/native
host work, and `lib/debug/backtrace.c`/`lib/debug/mutex.c` relying on WASM
stubs. Prefer an existing `lib/platform/wasm/` implementation or stub to a
new preprocessor island. If a public API must still exist, provide a safe WASM
stub with the same signature and explicit browser semantics.

The WASM CMake source list intentionally omits terminal `main.c` and blocking
session screen loops. JavaScript owns the browser render/event loop and calls
coarse session events, frame operations, and renderer frames. Never make a browser callback allocate, log heavily,
block, or wait for a native terminal condition.

## Why web.ascii-chat.com is possible

The browser does not have a separate ASCII renderer. The CLI's `--render-file`
backend is the key: the browser reuses the same media decoding, frame sizing,
options, and terminal-cell rendering path and sends its result to browser
surfaces instead of a native file/terminal. `libvterm` is central to terminal
cell semantics. The generated font data and the project's font/render support
(including FreeType-backed paths where enabled) are central to converting
pixels into stable glyph output. Read `lib/media/render/terminal.c`, the
renderer sources, `src/web/renderer/`, and `cmake/targets/Fonts.cmake` before
changing rendering. Preserve the allocation-free hot path, dimensions/stride,
palette, glyph, stabilization, and ownership rules.

WASM dependencies are ordinary linked libraries built for the Emscripten
target, not magic browser packages. The current WASM project explicitly wires
libsodium, PCRE2, ICU, Opus, Zstd, libvterm, FFTW, utf8proc, TOML, yyjson,
and generated fonts through `src/web/CMakeLists.txt` and `cmake/deps/`. In
addition, Emscripten supplies the runtime/glue library and the Embind runtime
when bindings are linked (the project may therefore produce more than one
compiler/runtime artifact, but do not assume a fixed count). Inspect the
actual link line and artifact manifest rather than guessing.

## Embind: the current ABI

Bindings are registered by static `EMSCRIPTEN_BINDINGS(...)` blocks. The
current binding families are:

- `audio.cpp`: `AudioProcessorBinding` (`valid`, `process`) and
  `OpusCodecBinding` (`valid`, `encode`, `decode`) for browser audio work.
- `av_state.cpp`: `AVStateBinding` (`valid`, `generation`, `begin`,
  `complete`, `current`, `active`, `beginStop`, `finishStop`) for lifecycle
  synchronization.
- `client_frames.cpp`: `validateFrame` for incoming frame validation.
- `discovery.cpp`: `DiscoveryBinding` (`valid`, `callbackToken`, `process`)
  plus `handoffEncode`/`handoffDecode`.
- `face.cpp`: `faceReset`, `faceDetect`, and `faceTick`.
- `frame.cpp`: stabilizer/capture helpers plus `MirrorFrameBinding`
  (`convert`, `transform`, `submitAudio`, `renderAudio`).
- `key_parser.cpp`: browser key/error APIs (`parseKey`, `keyParserError`,
  `errorSnapshot`, `errorFrame`, and dismissal).
- `mirror_session.cpp`: `initMirror`, `sessionRenderCleanup`, and
  `mirror_cleanup`.
- `networking.cpp`: client initialization/cleanup and the
  `ClientSessionBinding.process(event)` session boundary for handshake,
  packet, crypto-policy, addressing, and QUIC events.
- `peer_crypto.cpp`: `configureCrypto` and `PeerCryptoBinding.process(event)`
  for session crypto, reliable send/receive/tick, QUIC lifecycle/output, and
  packet sealing/opening.
- The old granular `protocol.cpp` binding was removed. Packet parsing,
  serialization, handshake, and crypto are reached through the client session
  event API and its TypeScript domain helpers.
- `session_host.cpp`: browser host lifecycle/render/audio plus
  `HostPacketBinding.receive`.
- `session_ui.cpp`: UI FPS/volume and `SessionUiBinding` for resize, loading,
  logs, media, key, tick, clear, and redraw.
- `terminal_renderer.cpp`: `AsciiRendererBinding` (`valid`, `error`,
  `dimensions`, `render`). It accepts a complete text frame and reuses its
  C++-held output buffer where possible.
- `test_pattern.cpp`: `TestPatternBinding` (`valid`, `render`).

Generated declarations in `web/packages/shared/src/wasm/generated/` are useful
for TypeScript but are not the source of truth. Change the C++ binding and
regenerate/verify the declarations and loaders used by the relevant module.

### Memory and object lifetime

Prefer passing strings, numbers, structured values, typed arrays, or Embind
objects through the binding. JavaScript should not manually allocate, copy,
index, and free C heap memory for ordinary application work. In particular,
do not build a parallel `Module._malloc`/`HEAPU8` protocol unless a measured,
obvious binary fast path truly requires it. Put ownership in the C++ binding
method or return an Embind value with a clear policy.

For an Embind class created in JS or returned by a C++ function, call `.delete()`
when the handle is no longer needed, preferably in `try/finally`. Use
`clone()` only when two independent lifetimes genuinely need the same object;
delete every clone. Do not rely on `FinalizationRegistry` for correctness.
Use `return_value_policy::reference()` for borrowed internal objects and
`take_ownership()`/smart pointers for owned returns when appropriate. A typed
memory view is a borrowed view: it becomes invalid if the C buffer changes or
is freed, so copy it before retaining it beyond the documented lifetime.

Current binding classes generally own their native resources and expose a
small method surface. Follow their constructors/destructors and `valid()`/
`error()` APIs instead of reaching behind them. If a binding returns a binary
result, make the ownership and validity window explicit in C++ and TypeScript.

Embind can expose true C++ inheritance, but JavaScript subclassing is not a
general replacement for a wrapper. Emscripten's supported route is
`allow_subclass()` with a `wrapper<T>`, virtual methods, and `EMSCRIPTEN_WRAPPER`
for a polymorphic C++ interface; `base<T>` handles bound C++ inheritance.
Use that only when the C++ design really calls back into a JS override. For
ascii-chat's concrete resource-owning bindings, use the generated classes
directly. If browser code needs presentation state, a small TypeScript helper
or React hook is fine, but do not create a handwritten class that merely wraps
every Embind method. Do not manually wrap a binding just to avoid using the
generated type.

For small C-to-JavaScript conveniences, `emscripten::val`, `EM_JS`, or
`EM_ASM` can be clearer than hand-marshalling memory. yyjson is vendored and
available when a real JSON document must cross a C boundary, but most paths
are nicer as C values plus Emscripten/Embind types; use JSON deliberately, not
as the default interchange format.

## Options and renderer package

`web/packages/shared/src/wasm/options.ts` is the browser/native options
contract. Its enums mirror native values (`ColorMode`, `RenderMode`,
`ColorFilter`, `TextRenderer`), it owns browser defaults/palettes, validates
option patches and UTF-8 palette limits, and the `Options` class applies
revision-aware atomic patches through `readOptions()`/`patchOptions()`. Keep
those numeric values synchronized with native option enums and treat revision
conflicts as a refresh/retry, not as permission to overwrite state.

The shared package's `AsciiRenderer`, hooks, raster worker, and utilities are
the intended consumer of `renderer-web`. Extend those APIs when adding a
renderer feature; do not make `web/web` privately reimplement the renderer.

The longer-term goal is to publish `renderer-web` as the open-source
`ascii-chat` npm package: a polished browser renderer anyone can use to render
ASCII art. Expect that package to grow around the same core with H.264/HEVC
support, face and frame stabilization, an FPS-controlled render loop, and a
callback hook that lets an application supply pixels to the renderer. Keep
those features reusable and browser-agnostic rather than coupling them to
`web.ascii-chat.com`.

## Browser video codecs

Browser capture uses WebCodecs `VideoEncoder` in
`web/web/src/network/VideoUploadEncoder.ts`. It probes the exact H.264 and
HEVC configurations with `VideoEncoder.isConfigSupported()`, and the selection
logic in `videoEncoding.ts`/`useVideoEncodings.ts` prefers HEVC in `auto` only
when the browser actually supports the tested configuration, then H.264, then
raw. Explicit requests must not silently become a different codec. Encoded
Annex-B access units are carried in ACIP H.264/H.265 frame packets, while
`BrowserVideoDecoder` uses the browser decoder path for received frames.

HEVC/H.265 support is browser/OS/device dependent; H.264 is the compatibility
fallback. Keep probing capability rather than keying off the browser name.
When changing codec selection, inspect packet capability bits, packet types,
WebRTC/WebTransport receive filters, encoder configuration, decoder behavior,
and the E2E tests in `web/web/tests/e2e/video-encoding.spec.ts` together.

## Crypto and browser transports

`web/web/{discovery,client}` use the same application crypto/session machinery
as the CLI through the client and peer-crypto bindings. The transport wrapper
differs: browsers use WSS/WebSocket fallback, WebRTC/DTLS, and WebTransport
HTTP/3/QUIC where available, with browser APIs handling the underlying socket,
TLS, and ICE mechanics. Native QUIC and browser WebTransport share ACIP
concepts but are not identical browser APIs. Do not claim that browser TLS
replaces ACIP authentication: transport security and application/session
authentication remain separate readiness gates. Read
`WebRTCSession.ts`, `WebTransportClientSession.ts`, `AcipQuicProtocol.ts`,
`connectionSecurity.ts`, `DiscoveryProtocol.ts`, and the C bindings before
changing handoff, rekey, or packet security.

Preserve the shared packet formats, authenticated records, epochs, readiness
checks, cancellation, and fallback semantics. Browser-only code should adapt
events and byte streams to the shared protocol; it should not invent a second
crypto handshake or packet format.

## A safe change loop

1. Identify the module and follow its path from CMake source list to binding,
   generated TypeScript declaration, loader, and browser caller.
2. Check whether the behavior belongs in shared `lib/`/`include/`, a WASM
   platform implementation, a binding, or browser TypeScript. Keep each layer
   honest.
3. Guard or stub only the native-only boundary. Preserve error propagation,
   tracked allocation, ownership, and lifecycle cleanup.
4. Build the narrow target with CMake. For a binding or ABI change, rebuild the
   affected artifact and run the relevant TypeScript/E2E tests; for renderer or
   media changes, include the renderer and codec tests.
5. Inspect generated artifacts and the manifest/source hashes. Never hand-edit
   generated `.js`, `.wasm`, or runtime copies to make a test pass.

Useful checks include:

```powershell
cmake --build build --target mirror-web client-web renderer-web
pnpm --dir web/web test
pnpm --dir web/web exec playwright test tests/e2e/video-encoding.spec.ts
```

Use the package scripts and current CI workflow when names differ. For native
tests, also follow `ascii-chat-testing`; for web deployment, follow
`ascii-chat-web-deploy`.

For Embind semantics and current compiler behavior, consult the official
Emscripten Embind documentation: https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html
