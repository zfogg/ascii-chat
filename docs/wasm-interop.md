# Browser WASM bindings

The mirror and client modules expose the C implementation through the C++
adapters in `src/web/bindings/`. Embind generates the declarations in
`web/packages/shared/src/wasm/generated/` from those adapters. The adapters
are only compiled for WASM; native APIs remain C APIs. `wasm-build.mjs` copies
the declarations from both builds and records their hashes alongside the WASM
artifacts. Rebuild and publish both modules together after changing a binding.

Browser code should prefer the shared TypeScript facades and generated binding
types. `SessionUiBinding`, `MirrorFrameBinding`, `TerminalRendererBinding`,
`AudioProcessorBinding`, `OpusCodecBinding`, and `PeerCryptoBinding` own their
C resources. An Embind object must be deleted once after its last use; facades
such as the renderer lifecycle and `PeerCryptoSession.close()` make repeated
cleanup safe. The C options and mirror initialization functions remain
module-local, so they must be initialized before creating objects that use
them. An object belongs to the WASM module and worker that created it; do not
transfer it or use it after that module is cleaned up.

Strings are copied at the boundary. Frame and audio inputs are copied into
reusable WASM-side storage. Renderer pixels and encrypted packets are copied
into JavaScript-owned arrays before returning to callers, including before a
peer output queue entry is released. No returned array is a borrowed view of
WASM memory, so it remains usable after a subsequent call, heap growth, or
object disposal. The framebuffer copy takes an existing `Uint8ClampedArray` so
the canvas path can reuse its `ImageData` allocation. Do not retain a
`typed_memory_view` across a call that may grow memory or across asynchronous
work.

Client bindings are grouped by responsibility: `networking.cpp` owns connection
setup and QUIC transport operations; `protocol.cpp` handles handshake and packet conversion; `common/opts.cpp`
handles settings conversion; `client_frames.cpp` validates and sends frames.
`discovery.cpp` binds discovery and handoff, `session_host.cpp` binds the shared
session host, and `av_state.cpp` owns asynchronous media lifecycle state. Their
bindings call their native APIs directly. Common byte
copies are shared, and these file boundaries do not change the JavaScript API.
The key parser and crypto configuration accept temporary byte arrays. Their
C++ adapters clear secret scratch buffers after the C call, and TypeScript
clears its temporary arrays in `finally`. The parser worker continues to unlink
its temporary SSH-key file. JavaScript strings and copies owned by browser APIs
cannot be reliably wiped. C error snapshots are copied into JavaScript values
before reporting them.

Embind is the browser-facing API, including options and host control. The browser renderer owns its `SessionUiBinding` directly and deletes it when the renderer is released; browser key conversion stays with the renderer. Options use `patchOptions({ width: 80, height: 40 }, revision)` for one validated atomic update and `readOptions()` for an owned snapshot object. Pass `-1` to accept any revision; stale revisions and invalid fields reject the entire patch. The TypeScript options facade supplies the default revision and returns the committed snapshot. The
C functions used by adapters are declared in the corresponding web headers,
which their implementations include as well. Emscripten's filesystem remains
available for temporary key files; the current heap view is used only to detect
input arrays that alias growable WASM memory. Keep high-frequency buffers
preallocated where practical, and test any additional copies in the actual
worker or rendering path.

The standalone [`ascii-chat` renderer package](https://github.com/zfogg/ascii-chat/blob/master/src/web/renderer/README.md)
uses the shared Embind frame and options adapters in `src/web/common/` and is
built by the separate `renderer-web` target. It produces an independently
initialized WASM module and npm package rather than adding another site asset.
Its Embind session object runs native cleanup in its destructor when the TypeScript facade is disposed. Its README documents its TypeScript API, ownership, browser isolation headers,
build, and real-browser performance test.
