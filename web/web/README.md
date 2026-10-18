# web.ascii-chat.com

Web-based clients for ascii-chat, providing browser access to mirror, client, and discovery modes.

## Overview

This is a React + Vite application that runs ascii-chat in the browser via WebAssembly. The core ascii-chat C code is
compiled to WASM using Emscripten, enabling real-time video chat with ASCII rendering directly in your browser.

## Stack

- **Vite+ (`vp`)** - Package manager, build tools, linting and tests
- **Vite** - Fast frontend build tool
- **React** - UI framework
- **Tailwind CSS** - Utility-first CSS
- **Emscripten** - C to WebAssembly compiler
- **TypeScript** - Type-safe JavaScript

## Development

### Local media in Mirror

Use **Upload Video** on `/mirror` to select or drop a video, animated GIF, or
still image. Files remain in the browser. The browser decodes video and plays
its synchronized audio; captured pixels go through the same C/WASM ASCII
renderer as the webcam. Videos play once and hold their last frame; GIFs retain their looping animation.
Pause/Play and Restart control the file. Toggle Media Playback (or `[`) switches
to the webcam/animation and back, preserving the file position and pause intent.
Video also has mute and volume controls. Stop, replacement, and navigation
release the media and cancel pending conversion. Files are not restored after
refresh; select the file again.

Video conversion streams fragmented MP4 into the browser's media buffer. Playback
starts once a conservative reserve is ready (at least three seconds, or the
whole clip for shorter media). The reserve grows when measured conversion is
slower than realtime. If conversion later slows, video and audio pause together
and resume after refilling. The UI shows seconds ready while conversion continues.
This cannot guarantee uninterrupted playback on every device or scene complexity.
The completed output is retained for explicit restart, capped at 128 MB; browsers
without compatible MediaSource support or files with unknown duration use the
complete-file conversion path.

MP4 and WebM audio tracks are inspected locally before playback. Known supported
audio codecs play directly; unknown or unsupported audio is converted to AAC.
This inspection also loads the converter and has the same 256 MB input limit. AVI, MKV, MOV,
MPEG, WMV, FLV, and other unsupported browser codecs use a lazy-loaded,
same-origin FFmpeg WASM worker to convert the first video and optional first
audio track to H.264/AAC MP4. This includes MPEG-4 Part 2/MP3 and MJPEG/PCM
AVI files. Conversion finishes before playback, can be cancelled, and is
limited to 256 MB input because it uses browser memory. Large files are better
converted to MP4/WebM locally. The converter fits video within 1280x720;
file capture scales to the ASCII grid (four input pixels per column, bounded
between 320 and 1280 pixels). Video-frame notifications drive capture, capped by
the configured FPS; the counter measures presented media frames, excluding UI
redraws. Actual throughput is limited by the browser and device. Corrupt files or
codecs missing from the decoder produce an error, not a silent fallback.

PNG, JPEG, WebP, AVIF, and BMP use browser image decoding. GIF uses explicit
frame timing and disposal/compositing (drawing an animated HTML image to a
canvas would freeze at its first frame). Transparent images composite onto
black. GIF logical dimensions are limited to 16 megapixels.

Synthetic fixtures in `tests/fixtures/media` contain moving patterns and a
440 Hz audio tone. Regenerate them with `node tests/fixtures/media/generate.mjs`
using native FFmpeg. The converter is `@ffmpeg/ffmpeg` (MIT) with the pinned
`@ffmpeg/core` build (GPL-2.0-or-later); upstream source and build instructions:
https://github.com/ffmpegwasm/ffmpeg.wasm/tree/n0.12.10.

### Setup

```bash
vp install
```

### Running Locally

```bash
vp dev
# It will print the dev url then livereload and refresh the page as you hack
```

### Building

```bash
vp run build
```

Builds the application to `dist/` for production.

### WASM Building

The WASM modules (mirror-web and client-web) are built automatically during the main build process:

```bash
vp run wasm:build
```

This invokes Emscripten to compile `src/web/mirror.c` and `src/web/client.c` to WebAssembly modules and places them in
`src/wasm/dist/`.

**Prerequisites:** Emscripten must be installed (`brew install emscripten` on macOS, `pacman -S emscripten` on Arch,
etc).

Configure the Emscripten CMake build before running this command. Set
`ASCII_CHAT_WASM_BUILD_DIR` to its build directory; the default is `../../build`.
The build compiles both modules and copies their JavaScript and WASM into
`src/wasm/dist` and `public/wasm`. Only `src/wasm/dist` is committed;
`public/wasm` is ignored and regenerated from that canonical bundle when Vite
starts for development, a build, or a preview. This includes the public JS
loaders used by the website demo. A SHA-256 manifest records artifacts and C
sources. `ASCII_CHAT_WASM_USE_PREBUILT=1` explicitly uses verified prebuilt
artifacts; a stale source hash fails the build.

## Network video encoding

Both `/client` (WebSocket) and `/discovery` (WebRTC) accept `?encoding=`:

- `hevc`: require browser HEVC/H.265 encoding. `hvec` and `h265` are aliases.
  If the browser/device cannot encode the requested configuration, capture stops
  with a visible error and a suggestion to use `?encoding=raw`. It never silently
  sends raw frames for an explicit HEVC request.
- `H.264`: require browser AVC/H.264 encoding. `h264`, `avc`, and `x264` are aliases (case-insensitive). The native host must include the H.264 network decoder from this change.
- `raw`: send RGB24 pixel frames without invoking WebCodecs.
- `auto` (default): try HEVC, then H.264, then raw pixels. Runtime encoder failures advance through the same order. Explicit codec choices never silently fall back.

Examples: `/client?encoding=hevc` and `/discovery?encoding=raw&session=blue-mountain-tiger`.
Invalid values show an error. Mirror mode is local and does not use this setting.
Compressed-video support is detected with `VideoEncoder.isConfigSupported` for the actual
size/frame rate and Annex B output; having HEVC playback support is insufficient.
The browser sends compressed camera pixels to the native host, which decodes
and renders ASCII. Return traffic remains ASCII frames.

Device setup includes a **Video encoding** selector containing the codecs this browser can encode. It initially selects the best available codec. `useVideoEncodings()` shares capability probing, the supported list, the best/current selection, and encoder creation between the connection, capture, and modal paths.

Selection precedence is `?encoding=` > saved preference > automatic. Pressing **Done** after changing the selector saves `ascii-chat.video-encoding` in local storage and updates the URL. Opening the modal, accepting its unchanged selection, cancelling, and visiting a URL override do not save an encoding. Removing the query override restores the saved preference. A saved or forced codec that becomes unavailable produces an error instead of silently changing the preference.

Browsers use their WebCodecs H.264 encoder. The native host decodes the AVC Annex B stream with FFmpeg; it is compatible with libx264 output. Native recording/export already uses libx264 when available. No libx264 WASM download is required.

The upload encoder requests keyframes on startup, every 60 encoded frames, and
when recreated for a size/frame-rate change. Transport congestion skips input
frames before encoding; encoded delta frames stay ordered. Camera restart or
reconnection creates a fresh encoder. Unsupported browsers retain raw uploads.

For real Chrome/native integration coverage, configure a native binary and run:

```powershell
$env:ASCII_CHAT_TEST_BINARY = 'C:/path/to/ascii-chat.exe'
# Select a reachable LAN interface for local native WebRTC on Windows.
$env:ASCII_CHAT_TEST_HOST = '192.168.1.100'
vp exec playwright test --config playwright.encoding.config.ts
```

The suite starts its own native server and discovery service, uses moving
synthetic camera input with real codecs/transports, and checks changing returned
ASCII and actual canvas pixels, packet types, upload volume, restart, resize,
HEVC/H.264/raw fallback, startup/runtime errors, and modal/URL/storage precedence.
HEVC tests require a device with an available HEVC encoder and do not skip when
it is unavailable. Failure tests deliberately remove WebCodecs, reject its
configuration probe, or inject an encoder failure.

## Discovery and WebRTC

Open `/discovery`, enter a native host's session name, and join. Connection
settings include STUN/TURN URLs, Connection route (Automatic or Relay only),
and optional TURN username/password overrides. The discovery WebSocket URL is
shown beside the session name. Leave both TURN credential fields blank to use
credentials supplied by discovery; custom credentials take precedence. Relay
only requires an authenticated TURN server and prohibits direct ICE paths.

Native discovery supports the same settings through `--webrtc-relay-only`,
`--turn-servers`, `--turn-username`, and `--turn-credential`, with corresponding
`ASCII_CHAT_*` environment variables and config fields. Omit
`--webrtc-relay-only` for automatic routing. Relay-only discovery never falls
back to TCP. Native servers support TURN overrides and relay-only mode when
started with `--discovery`; direct TCP client mode cannot use relay-only routing.
Credentials default to empty in both debug and release builds; native overrides
must supply both values, each at most 127 UTF-8 bytes. TURN authentication does
not change the session password. Credentials are kept out of browser URLs and
browser persistent storage.

The client signs
its ACDS join with an ephemeral identity, exchanges SDP and ICE candidates, and
sends ACIP video and Opus audio over an ordered WebRTC data channel. Enable the
microphone separately; disconnect stops camera and microphone tracks.

Plain `ws://` connections use ascii-chat encryption by default. `wss://` uses
TLS by default, and WebRTC uses DTLS. Transport encryption does not replace
password or identity authentication. Sessions requiring a verified long-term
browser identity are currently rejected with an explanatory error.

Phone access requires HTTPS, WSS signaling, and COOP/COEP headers for shared
WASM memory. Serve `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Configure reachable STUN/TURN
services and native host ICE settings. If TLS terminates at a proxy forwarding
to a plain native WS listener, configure that trusted upstream consistently:
the native listener otherwise expects the custom handshake. A direct native
WSS listener automatically uses TLS-only for sessions without password/key
requirements. Avoid caching fixed-name WASM files across deployments.

The native integration test uses a running host and discovery service:
`ASCII_CHAT_TEST_SESSION=<session> vp exec playwright test tests/e2e/webrtc-native.spec.ts`.
Its local signaling endpoint is `ws://127.0.0.1:28227`; it tests frames,
microphone controls, sustained connection and disconnect with fake media.

## Project Structure

```
src/               # Website source code
├── pages/              # Route pages (Mirror, Client, Discovery)
├── components/         # Reusable React components
├── wasm/               # WASM module integration and types
├── hooks/              # Custom React hooks
├── styles/             # Tailwind CSS + custom styles
└── App.tsx             # Main application component
scripts/            # Build and deployment scripts
└── build.sh            # Builds WASM, runs type checking, linting, and Vite
```

## Deployment

The application is deployed to Vercel and automatically triggers on git pushes to the main branch.

**Pre-commit checks:** TypeScript type checking, Prettier formatting, ESLint, and WASM compilation all run before
commits are accepted if you setup `../../git-hooks/pre-commit`.

## Shared Dependencies

This project uses the `@ascii-chat/shared` package from `../packages/shared` for common types and utilities.

## Browser Support

Modern browsers with WebAssembly support (Chrome, Firefox, Safari, Edge).

## Troubleshooting

Q: **WASM build fails with "emcc: command not found"**?
A: Install Emscripten.

## License

Same as ascii-chat (see root repository).
