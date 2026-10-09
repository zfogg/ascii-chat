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
