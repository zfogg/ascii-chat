# WebAssembly Browser Client Implementation Plan

**Document Version:** 1.0
**Author:** Claude Code (with zfogg)
**Date:** 2026-02-06
**Status:** Planning / Design Phase

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Architecture Overview](#architecture-overview)
3. [Code Reusability Analysis](#code-reusability-analysis)
4. [Mirror Mode Implementation](#mirror-mode-implementation)
5. [Client Mode Implementation](#client-mode-implementation)
6. [Discovery Mode Implementation](#discovery-mode-implementation)
7. [Server-Side Changes](#server-side-changes)
8. [Build System & Toolchain](#build-system--toolchain)
9. [Browser Integration Layer](#browser-integration-layer)
10. [mDNS Discovery Strategy](#mdns-discovery-strategy)
11. [Implementation Phases](#implementation-phases)
12. [Testing Strategy](#testing-strategy)
13. [Performance Considerations](#performance-considerations)
14. [Security Considerations](#security-considerations)

---

## Executive Summary

This document outlines the implementation plan for bringing ascii-chat to the browser using WebAssembly (WASM). The goal is to enable three modes:

1. **Mirror Mode** - Local webcam/media preview without networking
2. **Client Mode** - Full-featured client connecting via WebSocket (port 27226)
3. **Discovery Mode** - P2P connections via WebRTC DataChannels with ACDS signaling

**Key Design Principles:**
- **Maximize Code Reuse:** Compile existing C code to WASM where possible (crypto, ASCII conversion, SIMD)
- **Use Browser APIs:** Replace platform-specific code with native browser capabilities (getUserMedia, Web Audio)
- **Transport Abstraction:** Leverage existing `acip_transport_t` vtable pattern for WebSocket/WebRTC
- **Modern Frontend Stack:** Vite + Bun + Tailwind CSS for fast development and optimal production builds
- **Incremental Implementation:** Start with Mirror, then Client, then Discovery

**Technology Stack:**
- **Vite** - Lightning-fast dev server with HMR (Hot Module Replacement)
- **Bun** - Fast JavaScript runtime (3x faster than npm/yarn)
- **Tailwind CSS** - Utility-first CSS for rapid terminal-themed UI
- **TypeScript** - Type safety for browser code
- **Emscripten** - C/C++ to WASM compiler with SIMD support

**Deployment:**
- **Frontend:** `web.ascii-chat.com` (Vercel)
- **Discovery Service:** `discovery-service.ascii-chat.com` (VPS with Nginx)
- **Servers:** User-run (ad hoc, not hosted)

**Estimated Timeline:** 6-10 weeks for full implementation

---

## Architecture Overview

### High-Level Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                     Browser Environment                         │
│                                                                 │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │              HTML/JavaScript UI Layer                      │ │
│  │  - xterm.js (Terminal Emulator)                            │ │
│  │  - DOM Controls (settings, help, volume)                   │ │
│  │  - Canvas (media preview, status overlays)                 │ │
│  └──────────────┬─────────────────────────────────────────────┘ │
│                 │                                               │
│  ┌──────────────▼─────────────────────────────────────────────┐ │
│  │           Browser API Integration (JavaScript)             │ │
│  │  - getUserMedia() for video/audio capture                  │ │
│  │  - Web Audio API for audio processing                      │ │
│  │  - WebSocket for client networking                         │ │
│  │  - WebRTC DataChannel for P2P discovery                    │ │
│  └──────────────┬─────────────────────────────────────────────┘ │
│                 │ (JS ↔ WASM Boundary)                          │
│  ┌──────────────▼───────────────────────────────────────────────┐
│  │        WASM Core (Compiled from C)                         │ │
│  │  ┌───────────────────────────────────────────────────────┐ │ │
│  │  │ Video rocessing                                       │ │ │
│  │  │  - lib/video/ascii.c (RGB → ASCII conversion + SIMD)  │ │ │
│  │  │  - lib/video/palette.c (color mapping)                │ │ │
│  │  │  - lib/video/resize.c (frame resizing)                │ │ │
│  │  │  - lib/video/simd/*.c (SSE2/SSSE3/AVX2/NEON/SVE)      │ │ │
│  │  └───────────────────────────────────────────────────────┘ │ │
│  │  ┌───────────────────────────────────────────────────────┐ │ │
│  │  │ Cryptography                                          │ │ │
│  │  │  - lib/crypto/handshake/*.c (handshake protocol)      │ │ │
│  │  │  - lib/crypto/ssh/ssh_keys.c (SSH key parsing)        │ │ │
│  │  │  - lib/crypto/gpg/gpg_keys.c (GPG key parsing)        │ │ │
│  │  │  - lib/crypto/crypto.c (libsodium wrapper)            │ │ │
│  │  │  - lib/crypto/known_hosts.c (host verification)       │ │ │
│  │  └───────────────────────────────────────────────────────┘ │ │
│  │  ┌───────────────────────────────────────────────────────┐ │ │
│  │  │ Network Protocol                                      │ │ │
│  │  │  - lib/network/acip/*.c (ACIP packet handling)        │ │ │
│  │  │  - lib/network/packet.c (serialization)               │ │ │
│  │  │  - lib/network/crc32.c (checksums)                    │ │ │
│  │  │  - NEW: lib/network/websocket/protocol.c              │ │ │
│  │  └───────────────────────────────────────────────────────┘ │ │
│  │  ┌───────────────────────────────────────────────────────┐ │ │
│  │  │ Transport Layer (vtable abstraction)                  │ │ │
│  │  │  - acip_transport_t interface                         │ │ │
│  │  │  - NEW: acip_websocket_transport (C ↔ JS bridge)      │ │ │
│  │  │  - lib/network/webrtc/transport.c (adapt for browser) │ │ │
│  │  └───────────────────────────────────────────────────────┘ │ │
│  │  ┌───────────────────────────────────────────────────────┐ │ │
│  │  │ Discovery                                             │ │ │
│  │  │  - lib/discovery/*.c (ACDS protocol)                  │ │ │
│  │  │  - lib/network/webrtc/ice.c (ICE negotiation)         │ │ │
│  │  │  - lib/network/webrtc/sdp.c (SDP parsing)             │ │ │
│  │  └───────────────────────────────────────────────────────┘ │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    Server Environment (Unchanged)               │
│                                                                 │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  ascii-chat Server (Enhanced)                              │ │
│  │  - TCP Server (port 27224) - native clients                │ │
│  │  - NEW: WebSocket Server (port 27226) - browser clients    │ │
│  │  - ACDS Server (port 27225) - discovery signaling          │ │
│  │  - WebRTC STUN/TURN (optional, for NAT traversal)          │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

### Threading Model Adaptation

**Native Client (Current):**
```
Main Thread          → Connection orchestration + signal handling
Capture Thread       → V4L2/AVFoundation webcam capture
Audio Thread         → PortAudio capture/playback
Receive Thread       → TCP socket recv() + packet dispatch
Keepalive Thread     → Ping/pong heartbeat
```

**Browser Client (New):**
```
Main Thread          → Event loop coordination + UI updates
                       - getUserMedia() stream (browser thread)
                       - Web Audio API (browser thread)
                       - WebSocket callbacks (browser thread)
                       - requestAnimationFrame() render loop

WASM Worker          → Heavy computation (optional, for large frames)
(optional)             - ASCII conversion (if main thread is bottlenecked)
                       - Crypto operations (if >1000 ops/sec)
```

**Note:** Browsers handle audio/video/network I/O in separate threads internally. We don't need manual threading.

---

## Why This Stack? (Vite + Bun + Tailwind)

### Why Vite?

**Traditional bundlers (Webpack, Parcel) are slow:**
- Cold start: 10-30 seconds for medium projects
- HMR (Hot Module Replacement): 1-5 seconds per change
- Build time: 30-60 seconds for production

**Vite is fast:**
- Cold start: <1 second (native ES modules in dev)
- HMR: <50ms (surgical updates, no full rebuild)
- Build time: 5-15 seconds (Rollup with tree-shaking)

**Why it matters for ascii-chat:**
- WASM development requires frequent rebuilds → fast iteration critical
- Large codebase (60k+ LOC C) → bundler speed matters
- Real-time debugging (video/audio) → instant feedback essential

**Vite Features We Use:**
- ES modules in dev (no bundling until production)
- Out-of-box TypeScript support
- Built-in dev server with WebSocket proxy
- Optimized production builds (code splitting, tree-shaking)
- Asset optimization (WASM files served with correct MIME type)

### Why Bun?

**npm/yarn/pnpm install times (typical project):**
- npm: 30-60 seconds
- yarn: 20-40 seconds
- pnpm: 15-30 seconds
- **Bun: 3-10 seconds** (3-5x faster)

**Bun advantages:**
- Native TypeScript execution (no transpilation needed)
- Built-in bundler (we use Vite for dev server + production builds)
- Drop-in replacement for Node.js (same APIs)
- Single binary (no separate npm + node)
- Fast package installation (3-5x faster than npm/yarn)

**Real-world workflow improvement:**
```bash
# Old workflow (npm)
npm install              # 45 seconds
npm run build            # 30 seconds
# Total: 75 seconds

# New workflow (Bun + Vite)
bun install              # 8 seconds
bun run build            # 12 seconds (Vite)
# Total: 20 seconds (3.75x faster)
```

**Why it matters:**
- CI/CD builds 3x faster → quicker deploys
- Onboarding new developers faster (less waiting)
- Developer experience (less context switching while waiting)

### Why Tailwind CSS?

**Traditional CSS approach:**
```css
/* styles.css */
.terminal-button {
  background-color: #000;
  border: 2px solid #0f0;
  color: #0f0;
  padding: 8px 16px;
  font-family: monospace;
  text-transform: uppercase;
  transition: all 0.2s;
}

.terminal-button:hover {
  background-color: #0f0;
  color: #000;
}

.terminal-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
```

**Tailwind CSS approach:**
```html
<button class="bg-terminal-bg border-2 border-terminal-fg text-terminal-fg
               px-4 py-2 font-mono uppercase transition-all duration-200
               hover:bg-terminal-fg hover:text-terminal-bg
               disabled:opacity-50 disabled:cursor-not-allowed">
  Connect
</button>
```

**Advantages:**
1. **No context switching** - HTML and styles in same file
2. **Consistent design system** - Utilities enforce consistency
3. **Tiny production bundle** - PurgeCSS removes unused classes (~10KB vs 100KB+ custom CSS)
4. **Terminal theme built-in** - Custom color palette (terminal-bg, terminal-fg, terminal-accent)
5. **Responsive by default** - Mobile-first utilities (sm:, md:, lg:)

**Why it matters for ascii-chat:**
- Terminal aesthetic requires consistent monospace, green-on-black theme
- Rapid prototyping (no CSS file juggling)
- Small bundle size critical (mobile users, slow connections)
- Easy to maintain (styles colocated with components)

**Custom Terminal Theme:**
```javascript
// tailwind.config.js
module.exports = {
  theme: {
    extend: {
      colors: {
        'terminal-bg': '#000000',
        'terminal-fg': '#00ff00',
        'terminal-accent': '#00ffff',
        'terminal-error': '#ff0000',
        'terminal-warn': '#ffff00',
      }
    }
  }
}
```

Usage:
```html
<div class="bg-terminal-bg text-terminal-fg">
  <h1 class="text-terminal-accent">ascii-chat</h1>
  <p class="text-terminal-warn">Connection warning</p>
</div>
```

### Stack Comparison

| Aspect | Old Stack (Webpack + npm + CSS) | New Stack (Vite + Bun + Tailwind) |
|--------|----------------------------------|-------------------------------------|
| **Cold start** | 15-30s | <1s |
| **HMR** | 2-5s | <50ms |
| **Install** | 45s | 8s |
| **Build** | 30-60s | 10-15s |
| **CSS bundle** | 100-200KB | 10-20KB |
| **Config complexity** | High (webpack.config.js 200+ LOC) | Low (vite.config.ts 50 LOC) |
| **TypeScript** | Requires ts-loader/babel | Native support |
| **Developer experience** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

---

## Code Reusability Analysis

### Category A: Compile to WASM (C Code Reuse)

These components are **pure logic** with minimal/no platform dependencies. Compile directly to WASM:

#### **1. Video Processing (lib/video/)**
- ✅ `lib/video/ascii.c` - RGB to ASCII conversion (1000+ LOC)
  - Core algorithm: luminance calculation, character mapping
  - **CRITICAL:** Contains SIMD optimizations (SSE2/SSSE3/AVX2/NEON/SVE)
  - WASM SIMD support: Available in all modern browsers since 2021
  - **Benefit:** 3-5x performance boost vs scalar code
  - **Implementation:** Emscripten with `-msimd128` flag

- ✅ `lib/video/palette.c` - Color palette generation and mapping
  - Pure math: RGB color space transformations
  - No platform dependencies

- ✅ `lib/video/resize.c` - Frame resizing (nearest neighbor, bilinear)
  - **Alternative:** Use Canvas 2D API `drawImage()` for hardware acceleration
  - **Decision:** Use Canvas API initially, fall back to WASM if quality issues

- ✅ `lib/video/simd/*.c` - SIMD acceleration modules
  - `sse2.c`, `ssse3.c`, `avx2.c` (x86-64)
  - `neon.c`, `sve.c` (ARM/ARM64)
  - Emscripten supports WASM SIMD (128-bit vectors)
  - **Note:** Browser auto-detects CPU features, no runtime detection needed

#### **2. Cryptography (lib/crypto/)**
- ✅ `lib/crypto/handshake/*.c` - Handshake protocol (client.c, server.c, common.c)
  - State machine: INIT → KEY_EXCHANGE → AUTH → READY
  - Packet serialization, signature verification
  - **Dependency:** libsodium → replace with libsodium.js (WASM port)

- ✅ `lib/crypto/ssh/ssh_keys.c` - SSH key parsing (OpenSSH format)
  - PEM decoding, Ed25519 key extraction
  - **Use case:** Password-protected keys, SSH agent (via browser extension?)
  - **Alternative:** Use Web Crypto API for Ed25519 (experimental, not widely supported)

- ✅ `lib/crypto/gpg/gpg_keys.c` - GPG key parsing (OpenPGP format)
  - S-expression parsing, key unwrapping
  - **Benefit:** Reuse existing code for `gpg:keyid` authentication

- ✅ `lib/crypto/crypto.c` - libsodium wrapper functions
  - `crypto_box_easy()`, `crypto_sign_detached()`, `crypto_kdf_derive_from_key()`
  - **Replacement:** libsodium.js provides identical API

- ✅ `lib/crypto/known_hosts.c` - Server fingerprint verification
  - File I/O → replace with IndexedDB/LocalStorage (JS bridge)
  - Core verification logic: reusable

- ✅ `lib/crypto/pem_utils.c` - PEM encoding/decoding
  - Base64 + headers
  - **Alternative:** Use browser's `btoa()`/`atob()` (simpler)

#### **3. Network Protocol (lib/network/)**
- ✅ `lib/network/acip/*.c` - ACIP protocol implementation
  - **CRITICAL:** Packet serialization, CRC32, dispatch handlers
  - **Pure logic:** No socket operations, only buffer manipulation
  - `acip_client_receive_and_dispatch()` - packet demux
  - `acip_send_*()` - packet builders
  - **Reusability:** 95%+ (only replace transport layer)

- ✅ `lib/network/packet.c` - Packet header serialization
  - `send_packet()`, `receive_packet()` - format packets
  - **Dependency:** Transport abstraction (already designed for this!)

- ✅ `lib/network/crc32.c` - CRC32 checksums
  - Pure math, no dependencies

- ✅ `lib/network/compression.c` - Optional packet compression (zlib/lz4)
  - **Alternative:** Use browser's CompressionStream API (Brotli/gzip)

#### **4. Discovery (lib/discovery/)**
- ✅ `lib/discovery/*.c` - ACDS protocol (session registration, lookup)
  - `identity.c` - Generates "blue-mountain-tiger" style names
  - `session.c` - Session management state machine
  - `database.c` - SQLite wrapper → **Replace with IndexedDB (JS bridge)**

- ✅ `lib/network/webrtc/ice.c` - ICE candidate handling
  - Parsing/generation of ICE candidates
  - **Browser provides:** Native WebRTC ICE negotiation
  - **Reuse:** ICE candidate filtering logic

- ✅ `lib/network/webrtc/sdp.c` - SDP parsing
  - **Browser provides:** Native SDP handling via RTCPeerConnection
  - **Reuse:** SDP manipulation utilities (if needed)

#### **5. Utilities**
- ✅ `lib/util/aspect_ratio.c` - Aspect ratio calculations
- ✅ `lib/util/endian.c` - Byte order conversions
- ✅ `lib/util/fps.c` - Frame rate limiting
- ✅ `lib/util/time.c` - Timestamp utilities
- ✅ All pure math/logic utilities

### Category B: Replace with Browser APIs (No WASM Compilation)

These components use OS-specific APIs that don't exist in WASM. Replace with browser equivalents:

#### **1. Video Capture (lib/video/webcam/)**
- ❌ `webcam_v4l2.c` (Linux - ioctl, /dev/video*)
- ❌ `webcam_avfoundation.m` (macOS - AVFoundation framework)
- ❌ `webcam_mediafoundation.cpp` (Windows - DirectShow)
- ✅ **Replace with:** `navigator.mediaDevices.getUserMedia()`

**JavaScript Replacement:**
```javascript
// Browser video capture (replaces V4L2/AVFoundation)
async function initializeVideoCapture() {
  const stream = await navigator.mediaDevices.getUserMedia({
    video: {
      width: { ideal: 800 },
      height: { ideal: 600 },
      frameRate: { ideal: 30, max: 60 }
    }
  });

  // Draw to canvas for pixel access
  const video = document.createElement('video');
  video.srcObject = stream;
  await video.play();

  const canvas = document.createElement('canvas');
  const ctx = canvas.getContext('2d');

  return { video, canvas, ctx };
}

// Capture frame → WASM
function captureFrame(video, canvas, ctx) {
  canvas.width = video.videoWidth;
  canvas.height = video.videoHeight;
  ctx.drawImage(video, 0, 0);

  // Get pixel data (RGBA format)
  const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
  const rgbaBuffer = imageData.data; // Uint8ClampedArray

  // Pass to WASM for ASCII conversion
  return asciiConvert(rgbaBuffer, canvas.width, canvas.height);
}
```

#### **2. Audio (lib/audio/)**
- ❌ `lib/audio/audio.c` - PortAudio wrapper (platform-specific)
- ❌ PortAudio dependencies (ALSA, CoreAudio, WASAPI)
- ✅ **Replace with:** Web Audio API + getUserMedia

**JavaScript Replacement:**
```javascript
// Audio capture (replaces PortAudio)
async function initializeAudioCapture() {
  const stream = await navigator.mediaDevices.getUserMedia({
    audio: {
      echoCancellation: true,   // Native WebRTC AEC3 (better than compiled)
      noiseSuppression: true,   // Native noise gate
      autoGainControl: true     // Native AGC
    }
  });

  const audioContext = new AudioContext({ sampleRate: 48000 });
  const source = audioContext.createMediaStreamSource(stream);

  // Create processor for sample capture
  const processor = audioContext.createScriptProcessor(4096, 1, 1);
  processor.onaudioprocess = (e) => {
    const samples = e.inputBuffer.getChannelData(0); // Float32Array
    sendAudioToServer(samples); // Send to WASM/WebSocket
  };

  source.connect(processor);
  processor.connect(audioContext.destination);

  return { audioContext, processor };
}

// Audio playback (replaces PortAudio output)
function playAudioSamples(samples) {
  const audioContext = new AudioContext();
  const buffer = audioContext.createBuffer(1, samples.length, 48000);
  buffer.getChannelData(0).set(samples);

  const source = audioContext.createBufferSource();
  source.buffer = buffer;
  source.connect(audioContext.destination);
  source.start();
}
```

**Benefits:**
- ✅ **Native AEC3:** Browser's echo cancellation is hardware-accelerated, likely faster than WASM
- ✅ **Zero overhead:** No PortAudio → WASM bridge needed
- ✅ **Better device support:** Browsers handle device enumeration, hotplug, permissions

#### **3. Terminal Display (lib/session/display.c)**
- ❌ TTY control (tcsetattr, ioctl TIOCGWINSZ)
- ❌ ANSI escape sequences direct to stderr
- ✅ **Replace with:** xterm.js terminal emulator

**JavaScript Replacement:**
```javascript
import { Terminal } from 'xterm';
import 'xterm/css/xterm.css';

// Terminal rendering (replaces TTY control)
function initializeTerminal() {
  const term = new Terminal({
    cols: 120,
    rows: 40,
    theme: {
      background: '#000000',
      foreground: '#ffffff'
    },
    allowTransparency: false,
    fontSize: 14,
    fontFamily: 'Menlo, Monaco, "Courier New", monospace'
  });

  term.open(document.getElementById('terminal'));
  return term;
}

// Render ASCII frame (called from WASM)
function renderFrame(ansiString) {
  term.clear();
  term.write(ansiString); // xterm.js handles ANSI sequences
}
```

**xterm.js Features:**
- ✅ Full ANSI escape sequence support (colors, cursor movement)
- ✅ GPU-accelerated rendering (WebGL backend)
- ✅ Handles 256-color palette automatically
- ✅ Used by VS Code, GitHub Codespaces, Azure Cloud Shell

#### **4. Platform Abstractions (lib/platform/)**
- ❌ `lib/platform/posix/` - POSIX threads, signals, file I/O
- ❌ `lib/platform/windows/` - Windows threads, socket I/O
- ✅ **Replace with:** Browser APIs (no WASM compilation)

**Replacements:**
| Native | Browser Equivalent |
|--------|-------------------|
| `pthread_create()` | Web Workers (if needed) or async/await |
| `mutex_t` | JavaScript is single-threaded (no mutexes needed) |
| `signal(SIGTERM)` | `window.addEventListener('beforeunload')` |
| `socket()` | `WebSocket` or `RTCDataChannel` |
| `fopen()` | `fetch()`, `FileReader`, IndexedDB |
| `getenv()` | `localStorage.getItem()` |

#### **5. Networking (lib/network/tcp/)**
- ❌ `lib/network/tcp/client.c` - Raw TCP sockets (socket(), connect(), send(), recv())
- ❌ `lib/network/tcp/server.c` - TCP server (bind(), listen(), accept())
- ✅ **Replace with:** WebSocket (client), WebRTC DataChannel (P2P)

**Transport Implementation:**
- **Client Mode:** WebSocket to server port 27226
- **Discovery Mode:** WebRTC DataChannel (P2P)
- **Server Mode:** NOT POSSIBLE in browser (browsers can't listen on ports)

### Category C: Bridge Layer (WASM ↔ JavaScript)

These components need **bidirectional communication** between WASM and JavaScript:

#### **1. Transport Abstraction (NEW)**
- 🔄 `lib/network/websocket/transport.c` - WebSocket transport for ACIP
  - **Implementation:** C stub that calls JS functions via Emscripten
  - JS owns WebSocket, WASM calls send/recv via function pointers

**C → JS Bridge Example:**
```c
// lib/network/websocket/transport_browser.c (WASM-specific)
#include <emscripten.h>

// JavaScript function declarations (implemented in JS)
extern void js_websocket_send(const uint8_t *data, size_t len);
extern int js_websocket_recv(uint8_t *buffer, size_t max_len);

// WASM transport implementation
static asciichat_error_t websocket_send(acip_transport_t *transport,
                                        const void *data, size_t len) {
  // Call JavaScript WebSocket.send()
  js_websocket_send((const uint8_t *)data, len);
  return ASCIICHAT_OK;
}

static asciichat_error_t websocket_recv(acip_transport_t *transport,
                                        void **buffer, size_t *out_len,
                                        void **out_allocated_buffer) {
  uint8_t *recv_buffer = SAFE_MALLOC(MAX_PACKET_SIZE, uint8_t *);

  // Call JavaScript WebSocket recv (blocking via Emscripten async)
  int received = js_websocket_recv(recv_buffer, MAX_PACKET_SIZE);
  if (received <= 0) {
    SAFE_FREE(recv_buffer);
    return ERROR_NETWORK;
  }

  *buffer = recv_buffer;
  *out_len = (size_t)received;
  *out_allocated_buffer = recv_buffer;
  return ASCIICHAT_OK;
}

// Register transport methods
static const acip_transport_methods_t websocket_methods = {
  .send = websocket_send,
  .recv = websocket_recv,
  .close = websocket_close,
  .get_type = websocket_get_type,
  .get_socket = NULL, // No socket fd in browser
  .is_connected = websocket_is_connected,
  .destroy_impl = websocket_destroy_impl
};
```

**JavaScript WebSocket Bridge:**
```javascript
// JavaScript WebSocket implementation (called from WASM)
let websocket = null;
const messageQueue = [];

// Called from WASM
Module.js_websocket_send = function(dataPtr, len) {
  const data = new Uint8Array(Module.HEAPU8.buffer, dataPtr, len);
  websocket.send(data);
};

// Called from WASM (blocking)
Module.js_websocket_recv = function(bufferPtr, maxLen) {
  // Wait for message (Emscripten async support needed)
  if (messageQueue.length === 0) {
    return 0; // Would block
  }

  const message = messageQueue.shift();
  const copyLen = Math.min(message.length, maxLen);
  Module.HEAPU8.set(message.slice(0, copyLen), bufferPtr);
  return copyLen;
};

// WebSocket event handlers
websocket.onmessage = (event) => {
  const data = new Uint8Array(event.data);
  messageQueue.push(data);
};
```

#### **2. Crypto Key Storage**
- 🔄 `lib/crypto/known_hosts.c` - File I/O → IndexedDB bridge
- 🔄 SSH/GPG key loading - File system → File input or fetch()

**Bridge Strategy:**
```c
// WASM side - stub functions
extern int js_load_known_hosts(uint8_t *buffer, size_t max_len);
extern int js_save_known_hosts(const uint8_t *data, size_t len);

asciichat_error_t known_hosts_load(crypto_known_hosts_t *kh) {
  uint8_t buffer[MAX_KNOWN_HOSTS_SIZE];
  int len = js_load_known_hosts(buffer, sizeof(buffer));

  if (len > 0) {
    // Parse known_hosts format (existing C code)
    return known_hosts_parse(kh, buffer, len);
  }
  return ERROR_NOT_FOUND;
}
```

```javascript
// JavaScript side - IndexedDB storage
Module.js_load_known_hosts = function(bufferPtr, maxLen) {
  // Synchronous read from IndexedDB cache
  const data = knownHostsCache; // Loaded at startup
  if (data) {
    Module.HEAPU8.set(data, bufferPtr);
    return data.length;
  }
  return -1;
};

Module.js_save_known_hosts = async function(dataPtr, len) {
  const data = new Uint8Array(Module.HEAPU8.buffer, dataPtr, len);
  const db = await openDatabase();
  await db.put('known_hosts', data);
};
```

#### **3. mDNS Discovery**
- 🔄 `lib/network/mdns/discovery.c` - mDNS queries → **IMPOSSIBLE in browser**
- ❌ Browsers cannot send mDNS packets (security restriction)
- ✅ **Workaround:** Relay via discovery service (NOTE: we're not implementing this)

---

## Mirror Mode Implementation

**Goal:** Local webcam/media preview without networking. Simplest mode, foundation for others.

### Required Components

**Browser APIs (JavaScript):**
- `getUserMedia()` - Webcam capture
- Canvas 2D API - Frame extraction
- xterm.js - Terminal rendering
- requestAnimationFrame() - Render loop

**WASM Modules (Compiled C):**
- `lib/video/ascii.c` - RGB to ASCII conversion
- `lib/video/palette.c` - Color palette
- `lib/video/simd/*.c` - SIMD acceleration

It needs audio for --file and --url.

**NOT Needed:**
- ❌ Networking (no server connection)
- ❌ Crypto (no encryption)

### Implementation Steps

#### **Step 1: WASM Build for Mirror Mode**

```bash
# Emscripten compilation (minimal mirror mode)
emcc -O3 -msimd128 \
  -s WASM=1 \
  -s EXPORTED_FUNCTIONS='["_ascii_convert", "_palette_init", "_malloc", "_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["cwrap", "ccall"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=64MB \
  -s MAXIMUM_MEMORY=512MB \
  -I include/ \
  lib/video/ascii.c \
  lib/video/palette.c \
  lib/video/resize.c \
  lib/video/simd/*.c \
  lib/util/aspect_ratio.c \
  -o build/wasm/mirror.js
```

**Output Files:**
- `mirror.js` - Emscripten glue code
- `mirror.wasm` - WASM binary

#### **Step 2: HTML Shell**

```html
<!DOCTYPE html>
<html>
<head>
  <title>ascii-chat Mirror Mode</title>
  <link rel="stylesheet" href="xterm.css" />
  <style>
    body {
      background: #000;
      margin: 0;
      padding: 20px;
      font-family: monospace;
    }
    #terminal {
      width: 100%;
      height: 600px;
    }
    #controls {
      margin-bottom: 10px;
      color: #fff;
    }
  </style>
</head>
<body>
  <div id="controls">
    <button id="start-btn">Start Camera</button>
    <button id="stop-btn" disabled>Stop</button>
    <label>
      FPS: <span id="fps-display">0</span>
    </label>
  </div>
  <div id="terminal"></div>

  <script src="xterm.js"></script>
  <script src="mirror.js"></script>
  <script src="mirror-app.js"></script>
</body>
</html>
```

#### **Step 3: JavaScript Application**

```javascript
// mirror-app.js
import { Terminal } from 'xterm';

let Module; // Emscripten module
let terminal;
let videoStream;
let animationId;

// Initialize WASM module
Module = {
  onRuntimeInitialized: () => {
    console.log('WASM module loaded');
    initMirrorMode();
  }
};

// Initialize mirror mode
function initMirrorMode() {
  // Create terminal
  terminal = new Terminal({
    cols: 120,
    rows: 40,
    theme: { background: '#000000', foreground: '#ffffff' }
  });
  terminal.open(document.getElementById('terminal'));

  // Button handlers
  document.getElementById('start-btn').onclick = startMirror;
  document.getElementById('stop-btn').onclick = stopMirror;
}

// Start mirror mode
async function startMirror() {
  try {
    // Request webcam access
    videoStream = await navigator.mediaDevices.getUserMedia({
      video: { width: 800, height: 600, frameRate: 30 }
    });

    // Create video element
    const video = document.createElement('video');
    video.srcObject = videoStream;
    video.autoplay = true;
    await video.play();

    // Create canvas for pixel extraction
    const canvas = document.createElement('canvas');
    const ctx = canvas.getContext('2d');

    // Start render loop
    renderLoop(video, canvas, ctx);

    document.getElementById('start-btn').disabled = true;
    document.getElementById('stop-btn').disabled = false;
  } catch (err) {
    console.error('Failed to start mirror:', err);
    alert('Camera access denied or unavailable');
  }
}

// Render loop
function renderLoop(video, canvas, ctx) {
  const render = () => {
    // Extract frame
    canvas.width = video.videoWidth;
    canvas.height = video.videoHeight;
    ctx.drawImage(video, 0, 0);

    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
    const rgbaPixels = imageData.data; // Uint8ClampedArray

    // Convert to ASCII via WASM
    const asciiOutput = convertToASCII(rgbaPixels, canvas.width, canvas.height);

    // Render to terminal
    terminal.clear();
    terminal.write(asciiOutput);

    // Continue loop
    animationId = requestAnimationFrame(render);
  };

  render();
}

// WASM bridge: RGB → ASCII
function convertToASCII(rgbaPixels, width, height) {
  // Allocate WASM memory for pixels
  const pixelBytes = rgbaPixels.length;
  const pixelPtr = Module._malloc(pixelBytes);
  Module.HEAPU8.set(rgbaPixels, pixelPtr);

  // Call WASM ascii_convert()
  const asciiPtr = Module.ccall(
    'ascii_convert',
    'number',
    ['number', 'number', 'number', 'boolean', 'boolean', 'boolean'],
    [pixelPtr, width, height, true, true, false] // color=true, aspect=true, stretch=false
  );

  // Read ASCII string from WASM memory
  const asciiString = Module.UTF8ToString(asciiPtr);

  // Free memory
  Module._free(pixelPtr);
  Module._free(asciiPtr);

  return asciiString;
}

// Stop mirror
function stopMirror() {
  if (animationId) {
    cancelAnimationFrame(animationId);
  }
  if (videoStream) {
    videoStream.getTracks().forEach(track => track.stop());
  }
  terminal.clear();
  document.getElementById('start-btn').disabled = false;
  document.getElementById('stop-btn').disabled = true;
}
```

### Testing Mirror Mode

```bash
# Serve locally
python3 -m http.server 8000

# Open browser
firefox http://localhost:8000/mirror.html
```

**Expected Behavior:**
1. Click "Start Camera" → Browser prompts for webcam permission
2. Terminal displays live ASCII art from webcam
3. ~30 FPS rendering (measure with `performance.now()`)

### Mirror Mode Metrics

**Performance Targets:**
- ASCII conversion: <10ms per frame (800x600 → 120x40)
- Frame rate: 30 FPS (33ms budget)
- Memory usage: <100MB total
- WASM binary size: <2MB (gzipped)

---

## Client Mode Implementation

**Goal:** Full-featured client connecting to server via WebSocket on port 27226.

### Additional Requirements Beyond Mirror Mode

**New Components:**
- WebSocket connection to server
- Crypto handshake (WASM-compiled)
- Audio capture/playback (Web Audio API)
- Packet send/receive (ACIP protocol)
- SSH/GPG key support (optional)

### WASM Modules for Client Mode

**Compile to WASM:**
- Everything from Mirror Mode
- `lib/crypto/handshake/*.c` - Handshake protocol
- `lib/crypto/crypto.c` - libsodium wrapper (or use libsodium.js)
- `lib/crypto/ssh/ssh_keys.c` - SSH key parsing
- `lib/crypto/known_hosts.c` - Server verification
- `lib/network/acip/*.c` - ACIP protocol
- `lib/network/packet.c` - Packet serialization
- `lib/network/crc32.c` - Checksums

**Emscripten Build:**
```bash
emcc -O3 -msimd128 \
  -s WASM=1 \
  -s EXPORTED_FUNCTIONS='["_crypto_handshake_init", "_acip_send_packet", ...]' \
  -s USE_LIBSODIUM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -I include/ \
  lib/video/ascii.c \
  lib/crypto/handshake/*.c \
  lib/crypto/crypto.c \
  lib/crypto/ssh/ssh_keys.c \
  lib/network/acip/*.c \
  lib/network/packet.c \
  -o build/wasm/client.js
```

### WebSocket Transport Implementation

#### **Server-Side WebSocket Handler (C)**

**New File:** `lib/network/websocket/server.c`

```c
/**
 * @file network/websocket/server.c
 * @brief WebSocket server for browser clients
 *
 * Listens on port 27226 (separate from TCP port 27224).
 * Handles WebSocket handshake, then bridges to existing ACIP handlers.
 */

#include <ascii-chat/network/websocket/server.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/socket.h>

// WebSocket frame opcodes
#define WS_OPCODE_TEXT   0x01
#define WS_OPCODE_BINARY 0x02
#define WS_OPCODE_CLOSE  0x08
#define WS_OPCODE_PING   0x09
#define WS_OPCODE_PONG   0x0A

typedef struct {
  socket_t sockfd;
  bool handshake_complete;
  uint8_t *recv_buffer;
  size_t recv_buffer_len;
} websocket_conn_t;

/**
 * Perform WebSocket handshake (HTTP Upgrade)
 */
static asciichat_error_t websocket_handshake(socket_t sockfd) {
  // 1. Read HTTP GET request
  char request[4096];
  ssize_t n = socket_recv(sockfd, request, sizeof(request) - 1, 0);
  if (n <= 0) {
    return ERROR_NETWORK;
  }
  request[n] = '\0';

  // 2. Extract Sec-WebSocket-Key header
  char *key_start = strstr(request, "Sec-WebSocket-Key: ");
  if (!key_start) {
    return ERROR_NETWORK_PROTOCOL;
  }
  key_start += strlen("Sec-WebSocket-Key: ");
  char *key_end = strstr(key_start, "\r\n");
  if (!key_end) {
    return ERROR_NETWORK_PROTOCOL;
  }

  char client_key[64];
  size_t key_len = key_end - key_start;
  memcpy(client_key, key_start, key_len);
  client_key[key_len] = '\0';

  // 3. Compute Sec-WebSocket-Accept
  char accept_key[128];
  websocket_compute_accept_key(client_key, accept_key);

  // 4. Send HTTP 101 Switching Protocols response
  char response[512];
  snprintf(response, sizeof(response),
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Accept: %s\r\n"
    "\r\n",
    accept_key
  );

  ssize_t sent = socket_send(sockfd, response, strlen(response), 0);
  if (sent != (ssize_t)strlen(response)) {
    return ERROR_NETWORK;
  }

  log_info("WebSocket handshake complete");
  return ASCIICHAT_OK;
}

/**
 * Decode WebSocket frame
 */
static asciichat_error_t websocket_decode_frame(const uint8_t *frame, size_t frame_len,
                                                 uint8_t *opcode, uint8_t **payload,
                                                 size_t *payload_len) {
  if (frame_len < 2) {
    return ERROR_NETWORK_PROTOCOL;
  }

  // Byte 0: FIN + opcode
  bool fin = (frame[0] & 0x80) != 0;
  *opcode = frame[0] & 0x0F;

  // Byte 1: MASK + payload length
  bool masked = (frame[1] & 0x80) != 0;
  uint64_t payload_length = frame[1] & 0x7F;

  size_t header_len = 2;

  // Extended payload length
  if (payload_length == 126) {
    if (frame_len < 4) return ERROR_NETWORK_PROTOCOL;
    payload_length = (frame[2] << 8) | frame[3];
    header_len = 4;
  } else if (payload_length == 127) {
    if (frame_len < 10) return ERROR_NETWORK_PROTOCOL;
    // 64-bit length (big-endian)
    payload_length = 0;
    for (int i = 0; i < 8; i++) {
      payload_length = (payload_length << 8) | frame[2 + i];
    }
    header_len = 10;
  }

  // Masking key (4 bytes)
  uint8_t mask[4];
  if (masked) {
    if (frame_len < header_len + 4) return ERROR_NETWORK_PROTOCOL;
    memcpy(mask, frame + header_len, 4);
    header_len += 4;
  }

  // Validate frame length
  if (frame_len < header_len + payload_length) {
    return ERROR_NETWORK_PROTOCOL;
  }

  // Extract payload
  *payload = SAFE_MALLOC(payload_length, uint8_t *);
  memcpy(*payload, frame + header_len, payload_length);

  // Unmask payload (browser sends masked frames)
  if (masked) {
    for (size_t i = 0; i < payload_length; i++) {
      (*payload)[i] ^= mask[i % 4];
    }
  }

  *payload_len = payload_length;
  return ASCIICHAT_OK;
}

/**
 * Encode WebSocket frame (server → client, unmasked)
 */
static asciichat_error_t websocket_encode_frame(uint8_t opcode, const uint8_t *payload,
                                                 size_t payload_len, uint8_t **frame,
                                                 size_t *frame_len) {
  // Calculate frame size
  size_t header_len = 2;
  if (payload_len >= 126 && payload_len <= 65535) {
    header_len = 4;
  } else if (payload_len > 65535) {
    header_len = 10;
  }

  *frame_len = header_len + payload_len;
  *frame = SAFE_MALLOC(*frame_len, uint8_t *);

  // Byte 0: FIN=1, RSV=0, opcode
  (*frame)[0] = 0x80 | opcode;

  // Byte 1: MASK=0 (server doesn't mask), payload length
  if (payload_len < 126) {
    (*frame)[1] = payload_len;
  } else if (payload_len <= 65535) {
    (*frame)[1] = 126;
    (*frame)[2] = (payload_len >> 8) & 0xFF;
    (*frame)[3] = payload_len & 0xFF;
  } else {
    (*frame)[1] = 127;
    for (int i = 0; i < 8; i++) {
      (*frame)[2 + i] = (payload_len >> (56 - i * 8)) & 0xFF;
    }
  }

  // Copy payload
  memcpy(*frame + header_len, payload, payload_len);

  return ASCIICHAT_OK;
}

/**
 * Create WebSocket transport for ACIP protocol
 */
acip_transport_t *acip_websocket_transport_create(socket_t sockfd,
                                                   crypto_context_t *crypto_ctx) {
  // Perform WebSocket handshake
  asciichat_error_t result = websocket_handshake(sockfd);
  if (result != ASCIICHAT_OK) {
    log_error("WebSocket handshake failed");
    return NULL;
  }

  // Allocate transport
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(acip_transport_t), acip_transport_t *);
  websocket_conn_t *ws = SAFE_CALLOC(1, sizeof(websocket_conn_t), websocket_conn_t *);

  ws->sockfd = sockfd;
  ws->handshake_complete = true;

  transport->impl_data = ws;
  transport->methods = &websocket_methods;
  transport->crypto_ctx = crypto_ctx;

  log_info("WebSocket transport created (fd=%d)", sockfd);
  return transport;
}

/**
 * Send ACIP packet over WebSocket (binary frame)
 */
static asciichat_error_t websocket_send(acip_transport_t *transport,
                                        const void *data, size_t len) {
  websocket_conn_t *ws = (websocket_conn_t *)transport->impl_data;

  // Encode as WebSocket binary frame
  uint8_t *frame;
  size_t frame_len;
  asciichat_error_t result = websocket_encode_frame(WS_OPCODE_BINARY,
                                                     (const uint8_t *)data, len,
                                                     &frame, &frame_len);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Send frame
  ssize_t sent = socket_send(ws->sockfd, frame, frame_len, 0);
  SAFE_FREE(frame);

  if (sent != (ssize_t)frame_len) {
    return ERROR_NETWORK;
  }

  return ASCIICHAT_OK;
}

/**
 * Receive ACIP packet from WebSocket
 */
static asciichat_error_t websocket_recv(acip_transport_t *transport,
                                        void **buffer, size_t *out_len,
                                        void **out_allocated_buffer) {
  websocket_conn_t *ws = (websocket_conn_t *)transport->impl_data;

  // Read WebSocket frame header
  uint8_t header[14]; // Max header size (10 + 4 mask)
  ssize_t n = socket_recv(ws->sockfd, header, 2, 0);
  if (n <= 0) {
    return ERROR_NETWORK;
  }

  // Parse frame to get full size
  // ... (implementation similar to websocket_decode_frame)

  // Read full frame
  uint8_t *frame = SAFE_MALLOC(frame_len, uint8_t *);
  // ... recv remaining bytes

  // Decode frame
  uint8_t opcode;
  uint8_t *payload;
  size_t payload_len;
  asciichat_error_t result = websocket_decode_frame(frame, frame_len,
                                                     &opcode, &payload, &payload_len);
  SAFE_FREE(frame);

  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Handle opcodes
  if (opcode == WS_OPCODE_CLOSE) {
    SAFE_FREE(payload);
    return ERROR_NETWORK; // Connection closed
  }

  if (opcode == WS_OPCODE_PING) {
    // Send PONG response
    uint8_t *pong_frame;
    size_t pong_len;
    websocket_encode_frame(WS_OPCODE_PONG, payload, payload_len,
                          &pong_frame, &pong_len);
    socket_send(ws->sockfd, pong_frame, pong_len, 0);
    SAFE_FREE(pong_frame);
    SAFE_FREE(payload);
    return ERROR_WOULD_BLOCK; // Try again
  }

  if (opcode != WS_OPCODE_BINARY) {
    SAFE_FREE(payload);
    return ERROR_NETWORK_PROTOCOL; // Expected binary frame
  }

  // Return payload (caller must free)
  *buffer = payload;
  *out_len = payload_len;
  *out_allocated_buffer = payload;

  return ASCIICHAT_OK;
}

// Transport method table
static const acip_transport_methods_t websocket_methods = {
  .send = websocket_send,
  .recv = websocket_recv,
  .close = websocket_close,
  .get_type = websocket_get_type,
  .get_socket = websocket_get_socket,
  .is_connected = websocket_is_connected,
  .destroy_impl = websocket_destroy_impl
};
```

#### **Client-Side WebSocket (JavaScript)**

```javascript
// client-websocket.js
class AsciichatWebSocketClient {
  constructor(serverUrl) {
    this.serverUrl = serverUrl;
    this.ws = null;
    this.messageQueue = [];
    this.onMessageCallback = null;
  }

  async connect() {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.serverUrl);
      this.ws.binaryType = 'arraybuffer';

      this.ws.onopen = () => {
        console.log('WebSocket connected');
        resolve();
      };

      this.ws.onerror = (error) => {
        console.error('WebSocket error:', error);
        reject(error);
      };

      this.ws.onmessage = (event) => {
        const data = new Uint8Array(event.data);

        // Queue message for WASM recv()
        this.messageQueue.push(data);

        // Notify WASM (if callback registered)
        if (this.onMessageCallback) {
          this.onMessageCallback(data);
        }
      };

      this.ws.onclose = () => {
        console.log('WebSocket closed');
      };
    });
  }

  send(data) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(data);
    } else {
      throw new Error('WebSocket not connected');
    }
  }

  recv() {
    if (this.messageQueue.length > 0) {
      return this.messageQueue.shift();
    }
    return null; // Would block
  }

  close() {
    if (this.ws) {
      this.ws.close();
    }
  }
}

// Export for WASM bridge
window.AsciichatWebSocketClient = AsciichatWebSocketClient;
```

#### **WASM ↔ JavaScript Bridge**

```javascript
// Bridge WebSocket to WASM transport
let wsClient = null;

// Initialize WebSocket connection
Module.js_websocket_connect = function(urlPtr) {
  const url = Module.UTF8ToString(urlPtr);
  wsClient = new AsciichatWebSocketClient(url);

  return wsClient.connect()
    .then(() => 0) // Success
    .catch(() => -1); // Failure
};

// Send packet
Module.js_websocket_send = function(dataPtr, len) {
  const data = new Uint8Array(Module.HEAPU8.buffer, dataPtr, len);
  try {
    wsClient.send(data);
    return 0; // Success
  } catch (err) {
    console.error('WebSocket send failed:', err);
    return -1;
  }
};

// Receive packet (blocking)
Module.js_websocket_recv = function(bufferPtr, maxLen) {
  const message = wsClient.recv();
  if (message === null) {
    return 0; // Would block
  }

  const copyLen = Math.min(message.length, maxLen);
  Module.HEAPU8.set(message.slice(0, copyLen), bufferPtr);
  return copyLen;
};

// Close connection
Module.js_websocket_close = function() {
  if (wsClient) {
    wsClient.close();
    wsClient = null;
  }
};
```

### Audio Implementation (Web Audio API)

```javascript
// client-audio.js
class AsciichatAudioPipeline {
  constructor() {
    this.audioContext = null;
    this.micStream = null;
    this.processor = null;
    this.isCapturing = false;
  }

  async initialize() {
    // Request microphone access with AEC
    this.micStream = await navigator.mediaDevices.getUserMedia({
      audio: {
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true,
        sampleRate: 48000,
        channelCount: 1
      }
    });

    // Create audio context
    this.audioContext = new AudioContext({ sampleRate: 48000 });
    const source = this.audioContext.createMediaStreamSource(this.micStream);

    // Create processor (4096 samples = 85ms @ 48kHz)
    this.processor = this.audioContext.createScriptProcessor(4096, 1, 1);
    this.processor.onaudioprocess = (e) => {
      if (!this.isCapturing) return;

      const samples = e.inputBuffer.getChannelData(0); // Float32Array
      this.onAudioSamples(samples);
    };

    source.connect(this.processor);
    this.processor.connect(this.audioContext.destination);

    console.log('Audio pipeline initialized');
  }

  startCapture(onSamplesCallback) {
    this.onAudioSamples = onSamplesCallback;
    this.isCapturing = true;
  }

  stopCapture() {
    this.isCapturing = false;
  }

  playAudio(samples) {
    const buffer = this.audioContext.createBuffer(1, samples.length, 48000);
    buffer.getChannelData(0).set(samples);

    const source = this.audioContext.createBufferSource();
    source.buffer = buffer;
    source.connect(this.audioContext.destination);
    source.start();
  }

  destroy() {
    if (this.processor) {
      this.processor.disconnect();
    }
    if (this.micStream) {
      this.micStream.getTracks().forEach(track => track.stop());
    }
    if (this.audioContext) {
      this.audioContext.close();
    }
  }
}

// Export for WASM
window.AsciichatAudioPipeline = AsciichatAudioPipeline;
```

**WASM Bridge for Audio:**
```javascript
let audioPipeline = null;

Module.js_audio_init = async function() {
  audioPipeline = new AsciichatAudioPipeline();
  await audioPipeline.initialize();
  return 0;
};

Module.js_audio_start_capture = function() {
  audioPipeline.startCapture((samples) => {
    // Convert Float32Array to Int16Array (Opus expects 16-bit PCM)
    const int16Samples = new Int16Array(samples.length);
    for (let i = 0; i < samples.length; i++) {
      int16Samples[i] = Math.max(-32768, Math.min(32767, samples[i] * 32768));
    }

    // Send to WASM (call audio_samples_received callback)
    const samplePtr = Module._malloc(int16Samples.length * 2);
    Module.HEAP16.set(int16Samples, samplePtr / 2);
    Module._on_audio_samples_captured(samplePtr, int16Samples.length);
    Module._free(samplePtr);
  });
};

Module.js_audio_play = function(samplesPtr, numSamples) {
  const samples = new Float32Array(
    Module.HEAP16.buffer,
    samplesPtr,
    numSamples
  );
  audioPipeline.playAudio(samples);
};
```

### Client Mode Application Flow

```javascript
// client-app.js
let Module; // WASM module
let terminal;
let wsClient;
let audioPipeline;
let videoCapture;

async function initClient() {
  // 1. Initialize terminal
  terminal = new Terminal({ cols: 120, rows: 40 });
  terminal.open(document.getElementById('terminal'));

  // 2. Connect to server
  const serverUrl = 'ws://localhost:27226';
  await Module.js_websocket_connect(serverUrl);

  // 3. Perform crypto handshake (WASM)
  const handshakeResult = Module.ccall('crypto_client_handshake', 'number', [], []);
  if (handshakeResult !== 0) {
    throw new Error('Handshake failed');
  }

  // 4. Initialize audio
  await Module.js_audio_init();
  Module.js_audio_start_capture();

  // 5. Initialize video capture
  videoCapture = await initializeVideoCapture();

  // 6. Start render loop
  startClientLoop(videoCapture);

  // 7. Start receive thread (WASM)
  Module._start_receive_thread();
}

function startClientLoop(videoCapture) {
  const render = () => {
    // Capture frame
    const frame = captureFrame(videoCapture.video, videoCapture.canvas, videoCapture.ctx);

    // Send frame to server (WASM)
    Module._send_video_frame(frame.data, frame.width, frame.height);

    // Continue loop
    requestAnimationFrame(render);
  };

  render();
}

// Receive loop (called from WASM thread)
Module._on_server_frame_received = function(asciiPtr, len) {
  const asciiString = Module.UTF8ToString(asciiPtr);
  terminal.clear();
  terminal.write(asciiString);
};

// Start client on page load
window.onload = () => {
  Module.onRuntimeInitialized = () => {
    initClient().catch(err => {
      console.error('Client initialization failed:', err);
      alert('Failed to connect to server');
    });
  };
};
```

---

## Discovery Mode Implementation

**Goal:** P2P connections via WebRTC DataChannels with ACDS signaling.

### Key Differences from Client Mode

- **Transport:** WebRTC DataChannel instead of WebSocket
- **Signaling:** ACDS server (WebSocket) for ICE/SDP exchange
- **Topology:** Star (session creator connects to N participants)
- **No server relay:** Direct P2P connections (lower latency)

### WebRTC Integration

**Good News:** The existing `lib/network/webrtc/transport.c` already implements ACIP over DataChannels!

**Changes Needed:**
- Replace `libdatachannel` (C library) with browser's native `RTCPeerConnection` API
- Bridge WASM to JavaScript for signaling

#### **JavaScript WebRTC Manager**

```javascript
// discovery-webrtc.js
class AsciichatWebRTCManager {
  constructor(acdsUrl) {
    this.acdsUrl = acdsUrl;
    this.peerConnections = new Map(); // clientId → RTCPeerConnection
    this.dataChannels = new Map(); // clientId → RTCDataChannel
    this.sessionId = null;
  }

  async createSession() {
    // Register session with ACDS
    const response = await fetch(`${this.acdsUrl}/register`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ role: 'host' })
    });

    const data = await response.json();
    this.sessionId = data.session_id; // e.g., "blue-mountain-tiger"

    console.log(`Session created: ${this.sessionId}`);
    return this.sessionId;
  }

  async joinSession(sessionId) {
    this.sessionId = sessionId;

    // Create peer connection for host
    const pc = this.createPeerConnection('host');

    // Create data channel
    const dc = pc.createDataChannel('acip', {
      ordered: true,
      maxRetransmits: 3
    });

    this.setupDataChannel(dc, 'host');
    this.dataChannels.set('host', dc);

    // Create offer
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);

    // Send offer to host via ACDS
    await fetch(`${this.acdsUrl}/offer`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        session_id: sessionId,
        offer: offer.sdp,
        ice_candidates: []
      })
    });

    // Wait for answer
    await this.pollForAnswer('host');
  }

  createPeerConnection(clientId) {
    const config = {
      iceServers: [
        { urls: 'stun:stun.l.google.com:19302' },
        // Optional TURN server for NAT traversal
        // { urls: 'turn:turn.example.com', username: 'user', credential: 'pass' }
      ]
    };

    const pc = new RTCPeerConnection(config);

    // ICE candidate handling
    pc.onicecandidate = (event) => {
      if (event.candidate) {
        this.sendIceCandidate(clientId, event.candidate);
      }
    };

    // Connection state monitoring
    pc.onconnectionstatechange = () => {
      console.log(`Connection state (${clientId}): ${pc.connectionState}`);
      if (pc.connectionState === 'connected') {
        Module._on_peer_connected(clientId);
      }
    };

    // Data channel (for incoming connections)
    pc.ondatachannel = (event) => {
      this.setupDataChannel(event.channel, clientId);
      this.dataChannels.set(clientId, event.channel);
    };

    this.peerConnections.set(clientId, pc);
    return pc;
  }

  setupDataChannel(dc, clientId) {
    dc.binaryType = 'arraybuffer';

    dc.onopen = () => {
      console.log(`DataChannel opened: ${clientId}`);
    };

    dc.onclose = () => {
      console.log(`DataChannel closed: ${clientId}`);
    };

    dc.onmessage = (event) => {
      const data = new Uint8Array(event.data);

      // Pass to WASM for ACIP processing
      const dataPtr = Module._malloc(data.length);
      Module.HEAPU8.set(data, dataPtr);
      Module._on_datachannel_message(clientId, dataPtr, data.length);
      Module._free(dataPtr);
    };
  }

  sendData(clientId, data) {
    const dc = this.dataChannels.get(clientId);
    if (dc && dc.readyState === 'open') {
      dc.send(data);
    } else {
      throw new Error(`DataChannel not ready: ${clientId}`);
    }
  }

  async sendIceCandidate(clientId, candidate) {
    await fetch(`${this.acdsUrl}/ice`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        session_id: this.sessionId,
        client_id: clientId,
        candidate: candidate.candidate,
        sdpMid: candidate.sdpMid,
        sdpMLineIndex: candidate.sdpMLineIndex
      })
    });
  }

  async pollForAnswer(clientId) {
    // Poll ACDS for answer (long polling or WebSocket)
    while (true) {
      const response = await fetch(`${this.acdsUrl}/poll?session_id=${this.sessionId}`);
      const data = await response.json();

      if (data.answer) {
        const pc = this.peerConnections.get(clientId);
        await pc.setRemoteDescription({
          type: 'answer',
          sdp: data.answer
        });
        break;
      }

      // Add ICE candidates
      for (const cand of data.ice_candidates || []) {
        await pc.addIceCandidate(new RTCIceCandidate(cand));
      }

      await new Promise(resolve => setTimeout(resolve, 1000));
    }
  }
}
```

#### **WASM Bridge for WebRTC**

```javascript
let webrtcManager = null;

Module.js_webrtc_create_session = async function(acdsUrlPtr) {
  const acdsUrl = Module.UTF8ToString(acdsUrlPtr);
  webrtcManager = new AsciichatWebRTCManager(acdsUrl);

  const sessionId = await webrtcManager.createSession();
  return Module.allocateUTF8(sessionId);
};

Module.js_webrtc_join_session = async function(sessionIdPtr) {
  const sessionId = Module.UTF8ToString(sessionIdPtr);
  await webrtcManager.joinSession(sessionId);
  return 0;
};

Module.js_webrtc_send = function(clientIdPtr, dataPtr, len) {
  const clientId = Module.UTF8ToString(clientIdPtr);
  const data = new Uint8Array(Module.HEAPU8.buffer, dataPtr, len);

  try {
    webrtcManager.sendData(clientId, data);
    return 0;
  } catch (err) {
    console.error('WebRTC send failed:', err);
    return -1;
  }
};
```

### mDNS Workaround for Discovery Mode

**Problem:** Browsers cannot send mDNS packets (security restriction).

**Solution:** Server-side mDNS relay via ACDS.

#### **Architecture:**

```
┌─────────────────┐                 ┌─────────────────┐
│  Browser Client │                 │  Native Client  │
│  (no mDNS)      │                 │  (has mDNS)     │
└────────┬────────┘                 └────────┬────────┘
         │                                   │
         │ WebSocket                         │ mDNS broadcast
         │ (poll for sessions)               │ (announce session)
         │                                   │
         ▼                                   ▼
┌─────────────────────────────────────────────────────┐
│           ACDS Server (Enhanced)                    │
│  ┌──────────────────────────────────────────────┐  │
│  │  mDNS Listener (NEW)                          │  │
│  │  - Listens on port 5353 (mDNS)                │  │
│  │  - Captures _asciichat._tcp.local broadcasts  │  │
│  │  - Stores in session database                 │  │
│  └──────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────┐  │
│  │  Session Registry                             │  │
│  │  - mDNS-discovered sessions (from native)     │  │
│  │  - WebRTC-registered sessions (from browser)  │  │
│  └──────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

#### **Implementation:**

**Server-Side mDNS Relay (C):**
```c
// src/discovery-service/mdns_relay.c
void *mdns_listener_thread(void *arg) {
  // Listen on mDNS port 5353
  socket_t mdns_sock = socket_create_udp();
  socket_bind(mdns_sock, "224.0.0.251", 5353); // mDNS multicast group

  while (!should_exit()) {
    // Receive mDNS packet
    uint8_t buffer[4096];
    struct sockaddr_in from;
    ssize_t n = recvfrom(mdns_sock, buffer, sizeof(buffer), 0,
                         (struct sockaddr *)&from, &fromlen);

    // Parse mDNS query/response
    if (mdns_is_asciichat_announcement(buffer, n)) {
      // Extract session info
      char session_id[256];
      char ip_address[INET_ADDRSTRLEN];
      uint16_t port;

      mdns_parse_asciichat_record(buffer, n, session_id, ip_address, &port);

      // Store in ACDS database
      acds_register_mdns_session(session_id, ip_address, port, 60 /* TTL */);

      log_info("mDNS session discovered: %s at %s:%u", session_id, ip_address, port);
    }
  }

  return NULL;
}
```

**Browser-Side Query (JavaScript):**
```javascript
// Query ACDS for both WebRTC and mDNS sessions
async function discoverSessions() {
  const response = await fetch('http://localhost:27225/sessions');
  const data = await response.json();

  // Returns:
  // [
  //   { id: "blue-mountain-tiger", type: "webrtc", clients: 3 },
  //   { id: "red-ocean-wolf", type: "mdns", ip: "192.168.1.100", port: 27224 }
  // ]

  return data.sessions;
}
```

**Note:** Browser can connect to mDNS-discovered sessions via WebSocket (if server supports both TCP and WebSocket transports).

---

## Server-Side Changes

### WebSocket Server on Port 27226

**New Server Thread:**
```c
// src/server/websocket_listener.c
void *websocket_listener_thread(void *arg) {
  server_context_t *server_ctx = (server_context_t *)arg;

  // Create WebSocket listener socket
  socket_t ws_sock = socket_create();
  socket_bind(ws_sock, "0.0.0.0", 27226);
  socket_listen(ws_sock, 128);

  log_info("WebSocket server listening on port 27226");

  while (!server_should_exit(server_ctx)) {
    // Accept WebSocket connection
    struct sockaddr_in client_addr;
    socket_t client_sock = socket_accept(ws_sock, &client_addr);
    if (client_sock == INVALID_SOCKET_VALUE) {
      continue;
    }

    // Create WebSocket transport
    acip_transport_t *ws_transport = acip_websocket_transport_create(
      client_sock,
      server_ctx->crypto_ctx
    );

    if (!ws_transport) {
      socket_close(client_sock);
      continue;
    }

    // Add client (reuse existing TCP client handling!)
    const char *client_ip = inet_ntoa(client_addr.sin_addr);
    int client_id = add_client(server_ctx, ws_transport, client_ip);

    log_info("WebSocket client connected: %s (id=%d)", client_ip, client_id);
  }

  socket_close(ws_sock);
  return NULL;
}

// Start WebSocket listener in server main()
void server_init_websocket(server_context_t *server_ctx) {
  asciichat_thread_t thread;
  asciichat_thread_create(&thread, websocket_listener_thread, server_ctx);
  asciichat_thread_detach(thread);
}
```

**Key Insight:** Once WebSocket handshake completes, the rest of the server code is **unchanged**! The `acip_transport_t` abstraction handles everything.

### Server Configuration

```bash
# Start server (supports both TCP and WebSocket clients)
./build/bin/ascii-chat server \
  --port 27224 \                    # TCP for terminal clients (with libsodium encryption)
  --websocket-port 27226 \          # WebSocket for browser clients (wss://)
  --tls-cert fullchain.pem \        # TLS cert for wss:// (browser clients)
  --tls-key privkey.pem \           # TLS key for wss://
  --key server-identity.key         # Server identity (both transports)

# How encryption works:
# - Terminal clients (TCP :27224) → libsodium encrypts packets
# - Browser clients (wss:// :27226) → TLS encrypts, libsodium authenticates only
# - Server auto-detects transport type, skips double encryption

# Start discovery-service (WebSocket/TLS only)
./build/bin/ascii-chat discovery-service \
  --port 27227 \                    # WebSocket (wss://)
  --tls-cert fullchain.pem \        # TLS cert (Let's Encrypt)
  --tls-key privkey.pem             # TLS key
```

---

## Build System & Toolchain

### Modern Frontend Stack

**Technology Choices:**
- **Vite** - Lightning-fast dev server with HMR, optimized production builds
- **Bun** - Fast JavaScript runtime and package manager (3x faster than npm)
- **Tailwind CSS** - Utility-first CSS framework for rapid UI development
- **TypeScript** - Type safety for JavaScript code
- **xterm.js** - Terminal emulator (from npm via Bun)

### Project Structure

```
web/
├── package.json              # Bun package manifest
├── bun.lockb                 # Bun lockfile (binary format)
├── vite.config.ts            # Vite configuration
├── tailwind.config.js        # Tailwind CSS configuration
├── tsconfig.json             # TypeScript configuration
├── index.html                # Main entry point
├── public/                   # Static assets
│   ├── favicon.ico
│   ├── wasm/                 # WASM binaries (copied from build-wasm/)
│   │   ├── mirror.js
│   │   ├── mirror.wasm
│   │   ├── client.js
│   │   └── client.wasm
│   └── assets/
│       └── splash.txt        # ASCII art
├── src/
│   ├── main.ts               # Application entry point
│   ├── router.ts             # Client-side routing
│   ├── styles/
│   │   └── main.css          # Tailwind imports + custom styles
│   ├── pages/
│   │   ├── Home.ts           # Landing page
│   │   ├── Mirror.ts         # Mirror mode
│   │   ├── Client.ts         # Client mode
│   │   └── Discovery.ts      # Discovery mode
│   ├── components/
│   │   ├── Terminal.ts       # xterm.js wrapper
│   │   ├── VideoCapture.ts   # getUserMedia wrapper
│   │   ├── AudioPipeline.ts  # Web Audio wrapper
│   │   └── StatusBar.ts      # Connection status UI
│   ├── wasm/
│   │   ├── mirror.ts         # Mirror WASM module loader
│   │   ├── client.ts         # Client WASM module loader
│   │   └── types.ts          # WASM type definitions
│   ├── transport/
│   │   ├── websocket.ts      # WebSocket client
│   │   └── webrtc.ts         # WebRTC manager
│   └── utils/
│       ├── logger.ts         # Logging utilities
│       └── metrics.ts        # Performance metrics
└── dist/                     # Vite build output (generated)
    ├── index.html
    ├── assets/
    │   ├── index-[hash].js
    │   └── index-[hash].css
    └── wasm/
        ├── mirror.wasm
        └── client.wasm
```

### Initial Setup

**Initialize Project:**
```bash
# Navigate to web directory
cd web/

# Initialize Bun project
bun init -y

# Install dependencies
bun add vite @vitejs/plugin-vue typescript
bun add tailwindcss postcss autoprefixer
bun add xterm @xterm/addon-fit @xterm/addon-web-links
bun add -D @types/node

# Initialize Tailwind
bunx tailwindcss init -p
```

**package.json:**
```json
{
  "name": "ascii-chat-web",
  "version": "1.0.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "vite build",
    "preview": "vite preview",
    "type-check": "tsc --noEmit",
    "lint": "eslint src --ext .ts",
    "wasm:build": "cd .. && cmake -B build-wasm -DCMAKE_TOOLCHAIN_FILE=cmake/Emscripten.cmake && cmake --build build-wasm",
    "wasm:copy": "mkdir -p public/wasm && cp ../build-wasm/*.{js,wasm} public/wasm/",
    "prebuild": "bun run wasm:build && bun run wasm:copy"
  },
  "dependencies": {
    "xterm": "^5.3.0",
    "@xterm/addon-fit": "^0.10.0",
    "@xterm/addon-web-links": "^0.11.0",
    "hls.js": "^1.5.0"
  },
  "optionalDependencies": {
    "@ffmpeg/ffmpeg": "^0.12.10",
    "@ffmpeg/util": "^0.12.1"
  },
  "devDependencies": {
    "@types/node": "^20.11.0",
    "@vitejs/plugin-vue": "^5.0.3",
    "autoprefixer": "^10.4.17",
    "postcss": "^8.4.33",
    "tailwindcss": "^3.4.1",
    "typescript": "^5.3.3",
    "vite": "^5.0.12"
  }
}
```

### Vite Configuration

**vite.config.ts:**
```typescript
import { defineConfig } from 'vite';
import path from 'path';

export default defineConfig({
  root: './',
  publicDir: 'public',

  // Development server
  server: {
    port: 3000,
    host: '0.0.0.0',
    open: true,
    proxy: {
      // Proxy WebSocket connections to local server during development
      '/ws': {
        target: 'ws://localhost:27226',
        ws: true,
        changeOrigin: true,
        rewrite: (path) => path.replace(/^\/ws/, '')
      },
      // Proxy ACDS API
      '/api': {
        target: 'http://localhost:27225',
        changeOrigin: true,
        rewrite: (path) => path.replace(/^\/api/, '')
      }
    }
  },

  // Production build
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    sourcemap: true,
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true, // Remove console.log in production
        drop_debugger: true
      }
    },
    rollupOptions: {
      output: {
        manualChunks: {
          // Separate vendor chunks for better caching
          'xterm': ['xterm', '@xterm/addon-fit', '@xterm/addon-web-links'],
          'wasm-mirror': ['/public/wasm/mirror.js'],
          'wasm-client': ['/public/wasm/client.js']
        }
      }
    },
    // Optimize WASM loading
    assetsInlineLimit: 0 // Don't inline WASM files
  },

  // Resolve aliases
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
      '@wasm': path.resolve(__dirname, './public/wasm')
    }
  },

  // Optimize dependencies
  optimizeDeps: {
    include: ['xterm', '@xterm/addon-fit'],
    exclude: ['@wasm/mirror.js', '@wasm/client.js'] // Don't pre-bundle WASM
  }
});
```

### Tailwind Configuration

**tailwind.config.js:**
```javascript
/** @type {import('tailwindcss').Config} */
export default {
  content: [
    './index.html',
    './src/**/*.{js,ts,jsx,tsx,html}'
  ],
  theme: {
    extend: {
      colors: {
        // Terminal-inspired color scheme
        'terminal-bg': '#000000',
        'terminal-fg': '#00ff00',
        'terminal-accent': '#00ffff',
        'terminal-error': '#ff0000',
        'terminal-warn': '#ffff00',
        'terminal-dim': '#008800',
      },
      fontFamily: {
        mono: ['Menlo', 'Monaco', 'Courier New', 'monospace'],
      },
      animation: {
        'pulse-slow': 'pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite',
        'blink': 'blink 1s step-end infinite',
      },
      keyframes: {
        blink: {
          '0%, 50%': { opacity: '1' },
          '50.01%, 100%': { opacity: '0' },
        }
      }
    },
  },
  plugins: [],
};
```

**src/styles/main.css:**
```css
@tailwind base;
@tailwind components;
@tailwind utilities;

/* Custom terminal styles */
@layer components {
  .terminal-container {
    @apply bg-terminal-bg border-2 border-terminal-fg rounded-lg p-4;
    @apply font-mono text-terminal-fg;
  }

  .btn-terminal {
    @apply bg-terminal-bg border-2 border-terminal-fg text-terminal-fg;
    @apply px-4 py-2 font-mono uppercase tracking-wider;
    @apply hover:bg-terminal-fg hover:text-terminal-bg;
    @apply transition-all duration-200;
    @apply disabled:opacity-50 disabled:cursor-not-allowed;
  }

  .status-connected {
    @apply text-terminal-fg;
  }

  .status-connecting {
    @apply text-terminal-warn animate-pulse;
  }

  .status-disconnected {
    @apply text-terminal-error;
  }
}

/* xterm.js overrides */
.xterm {
  @apply font-mono;
}

.xterm-viewport {
  overflow-y: auto !important;
}
```

### TypeScript Configuration

**tsconfig.json:**
```json
{
  "compilerOptions": {
    "target": "ES2020",
    "useDefineForClassFields": true,
    "module": "ESNext",
    "lib": ["ES2020", "DOM", "DOM.Iterable"],
    "skipLibCheck": true,

    /* Bundler mode */
    "moduleResolution": "bundler",
    "allowImportingTsExtensions": true,
    "resolveJsonModule": true,
    "isolatedModules": true,
    "noEmit": true,

    /* Linting */
    "strict": true,
    "noUnusedLocals": true,
    "noUnusedParameters": true,
    "noFallthroughCasesInSwitch": true,

    /* Path aliases */
    "baseUrl": ".",
    "paths": {
      "@/*": ["./src/*"],
      "@wasm/*": ["./public/wasm/*"]
    }
  },
  "include": ["src/**/*.ts", "src/**/*.d.ts"],
  "exclude": ["node_modules", "dist", "build-wasm"]
}
```

### Emscripten Build Integration

**CMakeLists.txt additions:**
```cmake
# WASM build targets
if(EMSCRIPTEN)
  # Output directory for Vite integration
  set(WASM_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/web/public/wasm")

  # Mirror mode (minimal)
  add_executable(ascii-chat-mirror-wasm
    lib/video/ascii.c
    lib/video/palette.c
    lib/video/simd/*.c
    lib/util/aspect_ratio.c
  )

  set_target_properties(ascii-chat-mirror-wasm PROPERTIES
    OUTPUT_NAME "mirror"
    RUNTIME_OUTPUT_DIRECTORY "${WASM_OUTPUT_DIR}"
    LINK_FLAGS "-s WASM=1 \
                -s EXPORTED_FUNCTIONS='[\"_ascii_convert\",\"_malloc\",\"_free\"]' \
                -s EXPORTED_RUNTIME_METHODS='[\"cwrap\",\"ccall\",\"UTF8ToString\"]' \
                -s ALLOW_MEMORY_GROWTH=1 \
                -s INITIAL_MEMORY=64MB \
                -s MAXIMUM_MEMORY=512MB \
                -s MODULARIZE=1 \
                -s EXPORT_NAME='AsciichatMirrorModule' \
                -s ENVIRONMENT='web'"
  )

  # Client mode (full)
  add_executable(ascii-chat-client-wasm
    lib/video/ascii.c
    lib/crypto/handshake/*.c
    lib/crypto/crypto.c
    lib/network/acip/*.c
    lib/network/packet.c
    # ... (all client dependencies)
  )

  set_target_properties(ascii-chat-client-wasm PROPERTIES
    OUTPUT_NAME "client"
    RUNTIME_OUTPUT_DIRECTORY "${WASM_OUTPUT_DIR}"
    LINK_FLAGS "-s WASM=1 \
                -s USE_LIBSODIUM=1 \
                -s EXPORTED_FUNCTIONS='[...]' \
                -s ALLOW_MEMORY_GROWTH=1 \
                -s INITIAL_MEMORY=128MB \
                -s MAXIMUM_MEMORY=1GB \
                -s MODULARIZE=1 \
                -s EXPORT_NAME='AsciichatClientModule' \
                -s ASYNCIFY=1 \
                -s ENVIRONMENT='web'"
  )

  # Custom target to build WASM and notify Vite
  add_custom_target(wasm-build
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
    COMMENT "Building WASM modules for web client"
  )
endif()
```

### Build Commands

```bash
# Development workflow
cd web/

# Install dependencies (first time only)
bun install

# Build WASM modules
bun run wasm:build

# Start dev server with hot reload
bun run dev
# → http://localhost:3000

# Type checking
bun run type-check

# Production build (includes WASM build)
bun run build
# → Output: web/dist/

# Preview production build
bun run preview
# → http://localhost:4173

# Alternative: Build WASM from root
cd /path/to/ascii-chat
cmake -B build-wasm -DCMAKE_TOOLCHAIN_FILE=cmake/Emscripten.cmake
cmake --build build-wasm --target wasm-build
```

---

## Browser Integration Layer

### Modern Component Architecture

The web client uses **TypeScript** with a simple, modular architecture. No heavy frameworks—just clean, typed JavaScript with Vite bundling.

### Main Entry Point

**index.html:**
```html
<!DOCTYPE html>
<html lang="en" class="dark">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ascii-chat - Terminal Video Chat in Your Browser</title>
  <meta name="description" content="Web-based ASCII art video chat. Mirror mode, client mode, and P2P discovery.">

  <!-- Favicon -->
  <link rel="icon" type="image/x-icon" href="/favicon.ico">

  <!-- Preload critical assets -->
  <link rel="preload" href="/wasm/mirror.wasm" as="fetch" type="application/wasm" crossorigin>

  <!-- Content Security Policy -->
  <meta http-equiv="Content-Security-Policy"
        content="default-src 'self';
                 script-src 'self' 'wasm-unsafe-eval';
                 connect-src 'self' ws: wss: https://web.ascii-chat.com;
                 img-src 'self' data: blob:;
                 media-src 'self' blob:;
                 style-src 'self' 'unsafe-inline';">
</head>
<body class="bg-terminal-bg text-terminal-fg font-mono antialiased">
  <div id="app"></div>
  <script type="module" src="/src/main.ts"></script>
</body>
</html>
```

**src/main.ts:**
```typescript
import './styles/main.css';
import { initRouter } from './router';
import { HomePage } from './pages/Home';
import { MirrorPage } from './pages/Mirror';
import { ClientPage } from './pages/Client';
import { DiscoveryPage } from './pages/Discovery';

// Initialize client-side router
const routes = {
  '/': HomePage,
  '/mirror': MirrorPage,
  '/client': ClientPage,
  '/discovery': DiscoveryPage,
};

// Start application
document.addEventListener('DOMContentLoaded', () => {
  initRouter(routes);
});
```

**src/router.ts:**
```typescript
type PageComponent = (container: HTMLElement) => void;

export function initRouter(routes: Record<string, PageComponent>) {
  const app = document.getElementById('app');
  if (!app) throw new Error('App container not found');

  function navigate(path: string) {
    const component = routes[path] || routes['/'];
    app.innerHTML = ''; // Clear previous content
    component(app);

    // Update URL without page reload
    window.history.pushState({}, '', path);
  }

  // Handle browser back/forward
  window.addEventListener('popstate', () => {
    navigate(window.location.pathname);
  });

  // Handle link clicks
  document.addEventListener('click', (e) => {
    const target = e.target as HTMLElement;
    if (target.tagName === 'A' && target.getAttribute('href')?.startsWith('/')) {
      e.preventDefault();
      navigate(target.getAttribute('href')!);
    }
  });

  // Initial render
  navigate(window.location.pathname);
}
```

**src/pages/Home.ts:**
```typescript
export function HomePage(container: HTMLElement) {
  container.innerHTML = `
    <div class="min-h-screen flex flex-col items-center justify-center p-8">
      <!-- ASCII Art Logo -->
      <pre class="text-terminal-accent text-center mb-12 text-xs sm:text-sm md:text-base select-none">
     ___   _____ _____ _____ _____        _____ _   _   _ _____
    / _ \\ /  ___/  __ \\_   _|_   _|      /  __ \\ | | | | |_   _|
   / /_\\ \\\\ \\`--.| /  \\/ | |   | |  ______| /  \\/ |_| | | | | |
   |  _  | \\`--. \\ |     | |   | | |______| |   |  _  | | | | |
   | | | |/\\__/ / \\__/\\_| |_ _| |_       | \\__/\\ | | | |_| | |
   \\_| |_/\\____/ \\____/\\___/ \\___/        \\____/_| |_|\\___/  \\_/
      </pre>

      <h1 class="text-4xl md:text-6xl font-bold text-terminal-fg mb-4">
        ascii-chat <span class="text-terminal-accent">web</span>
      </h1>

      <p class="text-terminal-dim text-lg md:text-xl mb-12 text-center max-w-2xl">
        Terminal-based video chat in your browser. Real-time ASCII art rendering.
      </p>

      <!-- Mode Selection Cards -->
      <div class="grid grid-cols-1 md:grid-cols-3 gap-6 w-full max-w-5xl">
        <!-- Mirror Mode -->
        <a href="/mirror"
           class="terminal-container hover:scale-105 transition-transform cursor-pointer group">
          <div class="text-4xl mb-4 text-center">🎥</div>
          <h2 class="text-2xl font-bold text-terminal-accent mb-2">Mirror Mode</h2>
          <p class="text-terminal-dim mb-4">
            View your webcam as ASCII art. No networking required.
          </p>
          <ul class="text-sm text-terminal-dim space-y-1">
            <li>✓ Local rendering</li>
            <li>✓ SIMD acceleration</li>
            <li>✓ 30 FPS</li>
          </ul>
          <div class="mt-4 text-terminal-accent group-hover:translate-x-2 transition-transform">
            Launch →
          </div>
        </a>

        <!-- Client Mode -->
        <a href="/client"
           class="terminal-container hover:scale-105 transition-transform cursor-pointer group">
          <div class="text-4xl mb-4 text-center">💬</div>
          <h2 class="text-2xl font-bold text-terminal-accent mb-2">Client Mode</h2>
          <p class="text-terminal-dim mb-4">
            Connect to an ascii-chat server. Full video chat features.
          </p>
          <ul class="text-sm text-terminal-dim space-y-1">
            <li>✓ E2E encryption</li>
            <li>✓ Audio + Video</li>
            <li>✓ Multi-party</li>
          </ul>
          <div class="mt-4 text-terminal-accent group-hover:translate-x-2 transition-transform">
            Connect →
          </div>
        </a>

        <!-- Discovery Mode -->
        <a href="/discovery"
           class="terminal-container hover:scale-105 transition-transform cursor-pointer group">
          <div class="text-4xl mb-4 text-center">🔍</div>
          <h2 class="text-2xl font-bold text-terminal-accent mb-2">Discovery Mode</h2>
          <p class="text-terminal-dim mb-4">
            Peer-to-peer connections via WebRTC DataChannels.
          </p>
          <ul class="text-sm text-terminal-dim space-y-1">
            <li>✓ P2P direct</li>
            <li>✓ NAT traversal</li>
            <li>✓ Session codes</li>
          </ul>
          <div class="mt-4 text-terminal-accent group-hover:translate-x-2 transition-transform">
            Discover →
          </div>
        </a>
      </div>

      <!-- Footer -->
      <div class="mt-16 text-terminal-dim text-center">
        <p>
          Open source on
          <a href="https://github.com/zfogg/ascii-chat"
             class="text-terminal-accent hover:underline">
            GitHub
          </a>
        </p>
      </div>
    </div>
  `;
}
```

---

## mDNS Discovery Strategy

**Problem:** Browsers cannot send/receive mDNS packets (no access to raw UDP sockets on port 5353).

**Solutions:**

### Option 1: Server-Side mDNS Relay (Recommended)

ACDS server acts as mDNS bridge:
- Native clients broadcast mDNS announcements as usual
- ACDS listens on mDNS port, captures announcements
- Browser queries ACDS HTTP API for discovered sessions
- Browser connects via WebSocket (native server must support it)

**Pros:**
- ✅ Works across subnets (mDNS + ACDS)
- ✅ Browser sees both WebRTC and mDNS sessions
- ✅ No browser extension needed

**Cons:**
- ❌ Requires ACDS deployment
- ❌ LAN-only discovery requires same-subnet ACDS

### Option 2: Browser Extension (Experimental)

Chrome/Firefox extension with native messaging:
- Extension spawns native helper binary
- Helper performs mDNS queries
- Extension passes results to web page

**Pros:**
- ✅ True LAN discovery without server

**Cons:**
- ❌ Requires users to install extension
- ❌ Platform-specific (Windows/macOS/Linux binaries)
- ❌ Complex deployment

### Option 3: Fallback to Manual IP Entry

Browser client shows IP input field:
```
Enter server address: [192.168.1.100:27226]
```

**Pros:**
- ✅ Always works
- ✅ No dependencies

**Cons:**
- ❌ Manual configuration

### Recommended Implementation

**Use all three in priority order:**
1. Query ACDS for sessions (includes mDNS + WebRTC)
2. If ACDS unavailable, prompt for manual IP entry
3. (Future) Offer browser extension as optional enhancement

---

## Implementation Phases

### Phase 1: Mirror Mode (Week 1-2)

**Goal:** Prove WASM viability with multiple video sources (webcam, file, URL).

**Tasks:**
- [ ] Set up Emscripten toolchain
- [ ] Compile `lib/video/ascii.c` to WASM with SIMD
- [ ] Create HTML/JS shell with xterm.js
- [ ] Implement getUserMedia (webcam capture)
- [ ] Implement file input (`<input type="file">` for `--file`)
- [ ] Implement URL input (`<video src>` for `--url`)
- [ ] Create video source selector UI (Tailwind)
- [ ] Bridge Canvas → WASM → xterm.js
- [ ] Test native decoder formats (MP4, WebM, Ogg)
- [ ] Add HLS support (hls.js for non-Safari browsers)
- [ ] Test on Chrome/Firefox/Safari
- [ ] Measure performance (FPS, memory usage)

**Success Criteria:**
- ✅ 30 FPS ASCII rendering at 120x40 characters
- ✅ <100MB memory usage
- ✅ WASM binary <2MB gzipped
- ✅ Webcam works (default, no flags)
- ✅ `--file` works (MP4, WebM via native `<video>`)
- ✅ `--url` works (HTTP/HLS video streams)

**Video Source Implementation:**

**1. Webcam (Default - no flags):**
```typescript
// Default behavior when no --file or --url specified
const stream = await navigator.mediaDevices.getUserMedia({
  video: { width: 800, height: 600, frameRate: 30 }
});
```

**2. File Upload (`--file video.mp4`):**
```typescript
// Use native <video> element decoder
const video = document.createElement('video');
video.src = URL.createObjectURL(file);
video.loop = true; // Loop playback
await video.play();
```

**Supported Formats (Native):**
- ✅ MP4 (H.264, H.265) - 95%+ browsers
- ✅ WebM (VP8, VP9) - 90%+ browsers
- ✅ Ogg (Theora) - 80%+ browsers
- ❌ MKV, AVI, FLV - Use ffmpeg.wasm (Phase 1.5)

**3. URL Input (`--url https://example.com/video.mp4`):**
```typescript
const video = document.createElement('video');
video.src = url;
video.crossOrigin = "anonymous"; // CORS
await video.play();
```

**Supported:**
- ✅ HTTP video URLs (MP4, WebM)
- ✅ HLS (.m3u8) - Safari native, others via hls.js
- ❌ RTSP - Not possible in browsers
- ❌ RTMP - Requires proxy

**Phase 1.5: ffmpeg.wasm (Optional, Lazy Load)**

For exotic formats (MKV, AVI, FLV):

```bash
# Add as optional dependencies
bun add @ffmpeg/ffmpeg @ffmpeg/util
```

**Lazy Loading Strategy:**
```typescript
async function loadVideoFile(file: File) {
  const ext = file.name.split('.').pop()?.toLowerCase();

  // Native formats - fast path
  if (['mp4', 'webm', 'ogv'].includes(ext)) {
    return initWithNativeDecoder(file);
  }

  // Exotic formats - lazy load ffmpeg
  const confirmed = confirm(
    'Format requires ffmpeg.wasm (~30MB). Continue?'
  );

  if (confirmed) {
    const { FFmpeg } = await import('@ffmpeg/ffmpeg');
    return transcodeWithFFmpeg(file);
  }
}
```

**Benefits:**
- ✅ Initial bundle: <5MB (no ffmpeg)
- ✅ 90% of users never download ffmpeg
- ✅ Power users get full format support

### Phase 2: WebSocket Transport (Week 3)

**Goal:** Enable browser → server communication with transport-aware encryption.

**Tasks:**
- [ ] Implement WebSocket frame protocol (C)
- [ ] Add `acip_websocket_transport_create()` with TLS detection
- [ ] Add `provides_encryption()` method to `acip_transport_t`
- [ ] Refactor ACIP packet send/recv to check `provides_encryption()`
- [ ] Create WebSocket listener thread (port 27226) with TLS support
- [ ] Bridge WebSocket to existing `add_client()` flow
- [ ] Test: wss:// connection skips libsodium encryption
- [ ] Test: ws:// connection uses libsodium encryption (if enabled)
- [ ] Test: TCP connection still uses libsodium encryption

**Success Criteria:**
- ✅ wss:// (TLS): ACIP packets sent plaintext, TLS encrypts
- ✅ ws:// (no TLS): ACIP packets encrypted with libsodium
- ✅ TCP: ACIP packets encrypted with libsodium (unchanged)
- ✅ Server handles all three transports simultaneously
- ✅ Web client (wss://) and terminal client (TCP) can chat together

### Phase 3: Client Mode - Crypto (Week 4)

**Goal:** Authentication handshake working in browser (TLS encrypts, libsodium authenticates).

**Tasks:**
- [ ] Compile `lib/crypto/handshake/*.c` to WASM
- [ ] Modify handshake to support "auth-only mode" (no symmetric encryption)
- [ ] Integrate libsodium.js for Ed25519 signatures only
- [ ] Implement JS bridge for crypto functions
- [ ] Add IndexedDB storage for known_hosts
- [ ] Test: TLS encrypts packets, libsodium does auth handshake
- [ ] Test password authentication (`--password`)
- [ ] Test SSH key authentication (`--key` file upload)
- [ ] Test server key pinning (`--server-key`)
- [ ] Test client whitelist (`--client-keys` on server side)

**Success Criteria:**
- ✅ TLS connection established (wss://)
- ✅ ACIP auth handshake completes (client identity verified)
- ✅ Packets sent plaintext in ACIP (TLS encrypts at transport layer)
- ✅ No double encryption (efficiency)
- ✅ known_hosts verification works (IndexedDB storage)
- ✅ Password auth works (`--password secret`)
- ✅ SSH key auth works (user uploads private key)
- ✅ Server key pinning works (reject mismatched server identity)
- ✅ Web and terminal clients can connect to same server

### Phase 4: Client Mode - Audio (Week 5)

**Goal:** Two-way audio over WebSocket.

**Tasks:**
- [ ] Implement Web Audio API capture pipeline
- [ ] Bridge JS audio samples → WASM
- [ ] Implement audio playback (JS)
- [ ] Test echo cancellation (AEC3)
- [ ] Optimize latency (<200ms round-trip)

**Success Criteria:**
- Clear two-way audio
- No noticeable echo
- Latency <200ms

### Phase 5: Client Mode - Video (Week 6)

**Goal:** Full video chat working.

**Tasks:**
- [ ] Implement video capture loop (getUserMedia)
- [ ] Send video frames over WebSocket
- [ ] Receive ASCII frames from server
- [ ] Render to xterm.js
- [ ] Add FPS counter, stats overlay

**Success Criteria:**
- Two browser clients can video chat via server
- 30 FPS video
- Audio + video synchronized

### Phase 6: Discovery Mode - WebRTC (Week 7-8)

**Goal:** P2P connections via DataChannels.

**Tasks:**
- [ ] Implement WebRTC manager (JS)
- [ ] Bridge RTCPeerConnection → WASM transport
- [ ] Implement ACDS signaling (offer/answer/ICE)
- [ ] Test P2P connection establishment
- [ ] Add STUN/TURN fallback
- [ ] Test NAT traversal scenarios

**Success Criteria:**
- Browser can create WebRTC session
- Browser can join existing session
- DataChannel transports ACIP packets
- Works across NAT (with STUN)

### Phase 7: mDNS Relay (Week 9)

**Goal:** Browser sees mDNS-discovered sessions.

**Tasks:**
- [ ] Add mDNS listener to ACDS server
- [ ] Store mDNS sessions in database
- [ ] Expose HTTP API for session query
- [ ] Implement browser-side session discovery UI
- [ ] Test mixed WebRTC + mDNS sessions

**Success Criteria:**
- Native client broadcasts mDNS
- ACDS captures announcement
- Browser queries and sees session
- Browser connects via WebSocket

### Phase 8: Polish & Deployment (Week 10)

**Goal:** Production-ready web client.

**Tasks:**
- [ ] Add error handling, reconnection logic
- [ ] Implement settings UI (volume, resolution)
- [ ] Add help screen (keyboard shortcuts)
- [ ] Optimize WASM binary size (tree shaking)
- [ ] Add loading spinner, connection status
- [ ] Write user documentation
- [ ] Deploy to GitHub Pages (static hosting)

**Success Criteria:**
- Graceful error handling
- Works on mobile browsers (iOS Safari, Chrome Android)
- Documentation complete
- Deployed at `https://ascii-chat.github.io/`

---

## Testing Strategy

### Unit Tests

**WASM Modules:**
```javascript
// test/wasm/test_ascii_convert.js
import { AsciichatMirrorModule } from '../wasm/mirror.js';

describe('ASCII Conversion', () => {
  let Module;

  before(async () => {
    Module = await AsciichatMirrorModule();
  });

  it('converts RGB to ASCII', () => {
    const width = 4, height = 4;
    const pixels = new Uint8Array(width * height * 4);
    pixels.fill(255); // White image

    const pixelPtr = Module._malloc(pixels.length);
    Module.HEAPU8.set(pixels, pixelPtr);

    const asciiPtr = Module._ascii_convert(pixelPtr, width, height, true, true, false);
    const ascii = Module.UTF8ToString(asciiPtr);

    expect(ascii).to.contain('@'); // White → '@' character

    Module._free(pixelPtr);
    Module._free(asciiPtr);
  });
});
```

### Integration Tests

**WebSocket Transport:**
```bash
# test/integration/test_websocket_transport.sh
#!/bin/bash

# Start server with WebSocket enabled
./build/bin/ascii-chat server --websocket-port 27226 &
SERVER_PID=$!

sleep 2

# Start browser client (headless Chrome)
npx playwright test test/integration/websocket_client.spec.js

kill $SERVER_PID
```

**Playwright Test:**
```javascript
// test/integration/websocket_client.spec.js
const { test, expect } = require('@playwright/test');

test('WebSocket client connects to server', async ({ page }) => {
  await page.goto('http://localhost:8000/client.html');

  // Enter server address
  await page.fill('#server-url', 'ws://localhost:27226');
  await page.click('#connect-btn');

  // Wait for connection
  await expect(page.locator('#status')).toHaveText('Connected', { timeout: 5000 });

  // Verify terminal displays content
  const terminalContent = await page.locator('#terminal').textContent();
  expect(terminalContent.length).toBeGreaterThan(0);
});
```

### Performance Tests

**WASM SIMD Benchmark:**
```c
// test/perf/benchmark_ascii_simd.c
void benchmark_ascii_conversion(void) {
  const int width = 800, height = 600;
  uint8_t *pixels = SAFE_MALLOC(width * height * 4, uint8_t *);

  // Fill with test pattern
  for (int i = 0; i < width * height * 4; i++) {
    pixels[i] = rand() % 256;
  }

  // Warmup
  for (int i = 0; i < 10; i++) {
    char *ascii = ascii_convert(pixels, width, height, true, true, false, ...);
    SAFE_FREE(ascii);
  }

  // Benchmark
  uint64_t start = time_get_ns();
  for (int i = 0; i < 100; i++) {
    char *ascii = ascii_convert(pixels, width, height, true, true, false, ...);
    SAFE_FREE(ascii);
  }
  uint64_t end = time_get_ns();

  double avg_ms = (end - start) / 1000000.0 / 100.0;
  printf("ASCII conversion: %.2f ms per frame (%.1f FPS)\n", avg_ms, 1000.0 / avg_ms);

  SAFE_FREE(pixels);
}
```

**Expected Results:**
- Native (x86-64, AVX2): ~2-3ms (400+ FPS)
- WASM (SIMD128): ~5-8ms (125-200 FPS)
- WASM (scalar): ~20-30ms (33-50 FPS)

### Browser Compatibility Tests

**Target Browsers:**
- Chrome 100+ (Windows, macOS, Linux, Android)
- Firefox 100+ (Windows, macOS, Linux)
- Safari 15+ (macOS, iOS)
- Edge 100+ (Windows)

**Test Matrix:**
| Browser | getUserMedia | Web Audio | WebSocket | WebRTC | WASM SIMD |
|---------|--------------|-----------|-----------|--------|-----------|
| Chrome 120 | ✅ | ✅ | ✅ | ✅ | ✅ |
| Firefox 120 | ✅ | ✅ | ✅ | ✅ | ✅ |
| Safari 17 | ✅ | ✅ | ✅ | ✅ | ✅ |
| Edge 120 | ✅ | ✅ | ✅ | ✅ | ✅ |
| Mobile Safari | ✅ | ⚠️ (requires user gesture) | ✅ | ✅ | ✅ |
| Chrome Android | ✅ | ✅ | ✅ | ✅ | ✅ |

---

## Performance Considerations

### WASM Optimization Flags

**Emscripten Flags:**
```bash
-O3                    # Maximum optimization
-msimd128              # Enable WASM SIMD (128-bit vectors)
-flto                  # Link-time optimization
--closure 1            # Google Closure Compiler (minify JS glue)
-s ALLOW_MEMORY_GROWTH=1  # Dynamic memory allocation
-s INITIAL_MEMORY=64MB    # Initial heap size
-s MAXIMUM_MEMORY=512MB   # Max heap size (prevent OOM)
```

### JavaScript Optimization

**Memory Management:**
```javascript
// Reuse buffers to avoid GC pressure
const frameBuffer = new Uint8Array(800 * 600 * 4);

function captureFrame(video, canvas, ctx) {
  ctx.drawImage(video, 0, 0);
  const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);

  // Reuse buffer instead of allocating new
  frameBuffer.set(imageData.data);
  return frameBuffer;
}
```

**Batch Operations:**
```javascript
// Batch audio samples to reduce WASM calls
const BATCH_SIZE = 4096;
const sampleBuffer = new Float32Array(BATCH_SIZE);
let sampleCount = 0;

processor.onaudioprocess = (e) => {
  const samples = e.inputBuffer.getChannelData(0);

  sampleBuffer.set(samples, sampleCount);
  sampleCount += samples.length;

  if (sampleCount >= BATCH_SIZE) {
    // Send batch to WASM
    sendAudioBatch(sampleBuffer);
    sampleCount = 0;
  }
};
```

### Network Optimization

**WebSocket Binary Frames:**
- Use `binaryType = 'arraybuffer'` (not 'blob')
- Send ACIP packets directly without JSON wrapping
- Compress large frames (enable `permessage-deflate` extension)

**WebRTC DataChannel:**
- Set `ordered: true` for ACIP protocol (TCP-like semantics)
- Use `maxRetransmits: 3` (balance reliability vs latency)
- Monitor `bufferedAmount` to avoid congestion

---

## Security Considerations

### Same-Origin Policy

**Problem:** Browser restricts cross-origin WebSocket/WebRTC connections.

**Solutions:**
- **Development:** Use `localhost` or `127.0.0.1` (same-origin)
- **Production:** Deploy web client on same domain as server
  - Example: `https://chat.example.com` serves both HTML and WebSocket
- **CORS:** Add `Access-Control-Allow-Origin` header to ACDS API

### Content Security Policy (CSP)

**HTML Header:**
```html
<meta http-equiv="Content-Security-Policy"
      content="default-src 'self';
               script-src 'self' 'wasm-unsafe-eval';
               connect-src 'self' ws: wss:;
               img-src 'self' data:;
               media-src 'self' blob:;">
```

**Explanation:**
- `wasm-unsafe-eval` - Required for WASM instantiation
- `connect-src ws: wss:` - Allow WebSocket connections
- `media-src blob:` - Allow getUserMedia streams

### Crypto Key Storage

**Browser Storage Options:**
| Storage | Capacity | Persistence | Security |
|---------|----------|-------------|----------|
| localStorage | 5-10MB | Permanent | ❌ Plaintext, same-origin |
| sessionStorage | 5-10MB | Tab lifetime | ❌ Plaintext |
| IndexedDB | 50MB+ | Permanent | ❌ Plaintext, but isolated |
| Web Crypto API | N/A | Session | ✅ Non-extractable keys |

**Recommendation:**
- Store public keys (known_hosts) in IndexedDB
- Store private keys using Web Crypto API (non-extractable)
- Prompt for password on each session (no persistent password storage)

**Example:**
```javascript
// Generate non-extractable key pair
const keyPair = await crypto.subtle.generateKey(
  { name: 'Ed25519' },
  false, // non-extractable
  ['sign', 'verify']
);

// Store public key in IndexedDB
const publicKeyJwk = await crypto.subtle.exportKey('jwk', keyPair.publicKey);
await db.put('keys', { id: 'identity', public: publicKeyJwk });
```

### WebRTC Security

**Disable Host Candidates (Privacy):**
```javascript
const pc = new RTCPeerConnection({
  iceServers: [{ urls: 'stun:stun.l.google.com:19302' }],
  iceTransportPolicy: 'relay' // Force TURN (no direct IP leak)
});
```

**Note:** This requires TURN server (relays all traffic, higher latency).

---

## Deployment Checklist

### Before Launch

**Testing:**
- [ ] Test on Chrome, Firefox, Safari, Edge (latest versions)
- [ ] Test on mobile (iOS Safari 15+, Chrome Android 100+)
- [ ] Test WebSocket connection to discovery service (wss://discovery-service.ascii-chat.com:27227)
- [ ] Test WebSocket connection to user-run servers (ws://localhost:27226, wss://example.com:27226)
- [ ] Test WebRTC with STUN/TURN servers (NAT traversal scenarios)
- [ ] Verify HTTPS on frontend (web.ascii-chat.com)
- [ ] Verify TLS on discovery service (wss://discovery-service.ascii-chat.com)
- [ ] Run Lighthouse performance audit (target >90 score)
- [ ] Test WASM loading on slow connections (3G simulation)

**Security:**
- [ ] Enable Content Security Policy (CSP) headers
- [ ] Test WebSocket origin validation on discovery service
- [ ] Verify SSL certificates (Let's Encrypt auto-renewal for discovery service)
- [ ] Scan for vulnerabilities (bun audit, Snyk)

**Monitoring:**
- [ ] Add Google Analytics (GA4)
- [ ] Add Vercel Analytics (Web Vitals)
- [ ] Set up uptime monitoring (UptimeRobot or similar)
- [ ] Configure server logs (journalctl, log rotation)
- [ ] Add error reporting (Sentry for frontend, server logs for backend)
- [ ] Monitor WebSocket connection metrics (connections/sec, duration)

**Documentation:**
- [ ] Write user guide (getting started, browser requirements)
- [ ] Document API endpoints (ACDS HTTP API)
- [ ] Create troubleshooting guide (common errors, WebSocket failures)
- [ ] Add developer documentation (building, contributing)

**Performance:**
- [ ] Enable Brotli compression on Vercel
- [ ] Configure aggressive caching for static assets (1 year max-age)
- [ ] Preload critical WASM files (link rel="preload")
- [ ] Minimize initial bundle size (<500KB JS+CSS+WASM)
- [ ] Test on low-end devices (Raspberry Pi, old Android phones)

### Hosting Architecture for web.ascii-chat.com

**Domain Setup:**
- **Frontend:** `web.ascii-chat.com` - Static site (Vercel)
- **Discovery Service:** `discovery-service.ascii-chat.com` - WebSocket signaling (port 27227, VPS)
- **Servers:** User-run (ad hoc) - Users specify address (e.g., `ws://192.168.1.100:27226`)

**Architecture:**
- Frontend hosted on Vercel Edge Network (global CDN) for fast loading
- Discovery service is the only hosted backend (for WebRTC signaling)
- Servers are NOT hosted - users run them locally or on their own VPSs
- Web client connects to user-specified server address (just like native client)

### Frontend Deployment (Vercel)

**Setup:**
```bash
# Install Vercel CLI
bun add -g vercel

# Login to Vercel
vercel login

# Initialize project (from web/ directory)
cd web/
vercel

# Production deploy
bun run build
vercel --prod
```

**Vercel Configuration (vercel.json):**
```json
{
  "name": "ascii-chat-web",
  "framework": "vite",
  "buildCommand": "bun run build",
  "outputDirectory": "dist",
  "installCommand": "bun install",
  "regions": ["iad1", "sfo1", "lhr1", "hnd1"],
  "headers": [
    {
      "source": "/wasm/(.*)",
      "headers": [
        {
          "key": "Cache-Control",
          "value": "public, max-age=31536000, immutable"
        },
        {
          "key": "Content-Type",
          "value": "application/wasm"
        }
      ]
    },
    {
      "source": "/(.*)",
      "headers": [
        {
          "key": "Cross-Origin-Embedder-Policy",
          "value": "require-corp"
        },
        {
          "key": "Cross-Origin-Opener-Policy",
          "value": "same-origin"
        }
      ]
    }
  ],
  "env": {
    "VITE_DISCOVERY_WS_URL": "wss://discovery-service.ascii-chat.com:27227"
  }
}
```

**GitHub Actions for Auto-Deploy:**
```yaml
# .github/workflows/deploy-web.yml
name: Deploy Web Client

on:
  push:
    branches: [main]
    paths:
      - 'web/**'
      - 'lib/**'
      - 'build-wasm/**'

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Setup Bun
        uses: oven-sh/setup-bun@v1
        with:
          bun-version: latest

      - name: Setup Emscripten
        uses: mymindstorm/setup-emsdk@v14
        with:
          version: latest

      - name: Build WASM modules
        run: |
          cmake -B build-wasm -DCMAKE_TOOLCHAIN_FILE=cmake/Emscripten.cmake
          cmake --build build-wasm

      - name: Install dependencies
        working-directory: web
        run: bun install

      - name: Build frontend
        working-directory: web
        run: bun run build
        env:
          VITE_DISCOVERY_WS_URL: wss://discovery-service.ascii-chat.com:27227

      - name: Deploy to Vercel
        uses: amondnet/vercel-action@v25
        with:
          vercel-token: ${{ secrets.VERCEL_TOKEN }}
          vercel-org-id: ${{ secrets.VERCEL_ORG_ID }}
          vercel-project-id: ${{ secrets.VERCEL_PROJECT_ID }}
          working-directory: web
          vercel-args: '--prod'
```

### Discovery Service Deployment (VPS - discovery-service.ascii-chat.com)

**Note:** This is the ONLY hosted backend service. Servers are user-run.

**Transport-Aware Crypto Architecture:**

The server/discovery-service must handle two transport types with different encryption strategies:

### Terminal Clients (TCP Transport)
```
TCP Connection (unencrypted)
└── ACIP Protocol
    ├── Handshake: libsodium (X25519 key exchange, Ed25519 signatures)
    └── Data: libsodium encrypted (XSalsa20-Poly1305)
```

**Flags:**
- Default: libsodium encryption enabled
- `--no-encrypt`: Disable libsodium (plaintext ACIP)

### Web Clients (WebSocket/TLS Transport)
```
TLS Connection (Let's Encrypt + BearSSL)
└── WebSocket
    └── ACIP Protocol
        ├── Handshake: libsodium (authentication only, no symmetric key)
        └── Data: NO libsodium encryption (TLS already encrypts)
```

**Detection:**
- Server detects transport type: `transport->provides_encryption()`
- If WebSocket/TLS: Skip libsodium packet encryption (TLS handles it)
- If TCP: Use libsodium packet encryption
- Authentication handshake still happens (client identity verification)

**Why This Design?**
- ✅ **No double encryption** - TLS OR libsodium, not both
- ✅ **Same ACIP protocol** - Per-packet encryption flags already exist
- ✅ **Compatibility** - Terminal and web clients connect to same server
- ✅ **Flexible** - `--no-encrypt` works for both transport types

**Refactor Required:**
```c
// lib/network/acip/transport.h
typedef struct acip_transport_methods {
  // ... existing methods ...

  // NEW: Does this transport provide encryption?
  bool (*provides_encryption)(acip_transport_t *transport);
} acip_transport_methods_t;

// Implementation:
bool tcp_provides_encryption(acip_transport_t *t) {
  return false; // TCP doesn't encrypt, use libsodium
}

bool websocket_tls_provides_encryption(acip_transport_t *t) {
  websocket_transport_data_t *ws = (websocket_transport_data_t *)t->impl_data;
  return ws->is_tls; // If wss://, TLS encrypts
}

// Usage in packet send:
asciichat_error_t acip_send_packet(...) {
  bool transport_encrypts = transport->methods->provides_encryption(transport);

  if (transport_encrypts) {
    // Send plaintext ACIP packet (TLS encrypts at transport layer)
    send_raw_packet(transport, packet_data, packet_len);
  } else {
    // Encrypt with libsodium, then send
    encrypt_and_send_packet(transport, crypto_ctx, packet_data, packet_len);
  }
}
```

**Recommended VPS:**
- **Hetzner Cloud** - €4.51/month (CAX11: 2 vCPU ARM, 4GB RAM, 40GB SSD)
- **DigitalOcean** - $6/month (Basic Droplet: 1 CPU, 1GB RAM, 25GB SSD)
- **AWS Lightsail** - $5/month (t4g.small: 2 vCPU ARM, 2GB RAM)

**Server Setup Script:**
```bash
#!/bin/bash
# deploy-discovery-service.sh - Run on VPS

set -e

# Install dependencies
apt-get update
apt-get install -y build-essential cmake git certbot

# Clone repository
cd /opt
git clone https://github.com/zfogg/ascii-chat.git
cd ascii-chat

# Build discovery service (with BearSSL support)
cmake --preset default -B build -DUSE_BEARSSL=ON
cmake --build build

# Get Let's Encrypt certificate (standalone mode)
# Note: Port 80 must be available temporarily
certbot certonly --standalone \
  -d discovery-service.ascii-chat.com \
  --non-interactive --agree-tos \
  -m admin@ascii-chat.com

# Create systemd service for discovery-service
cat > /etc/systemd/system/ascii-chat-discovery.service <<EOF
[Unit]
Description=ascii-chat Discovery Service (WebSocket with TLS)
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/ascii-chat
ExecStart=/opt/ascii-chat/build/bin/ascii-chat discovery-service \\
  --port 27227 \\
  --tls-cert /etc/letsencrypt/live/discovery-service.ascii-chat.com/fullchain.pem \\
  --tls-key /etc/letsencrypt/live/discovery-service.ascii-chat.com/privkey.pem \\
  --key /opt/ascii-chat/keys/discovery-service-identity.key \\
  --client-keys /opt/ascii-chat/keys/allowed-clients.pub
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

# Note: Running as root to access /etc/letsencrypt (alternative: copy certs with proper permissions)

# Enable and start service
systemctl daemon-reload
systemctl enable ascii-chat-discovery
systemctl start ascii-chat-discovery

# Setup automatic certificate renewal
cat > /etc/systemd/system/certbot-renew.service <<'EOF'
[Unit]
Description=Renew Let's Encrypt certificates

[Service]
Type=oneshot
ExecStart=/usr/bin/certbot renew --quiet --deploy-hook "systemctl restart ascii-chat-discovery"
EOF

cat > /etc/systemd/system/certbot-renew.timer <<'EOF'
[Unit]
Description=Renew Let's Encrypt certificates daily

[Timer]
OnCalendar=daily
RandomizedDelaySec=1h
Persistent=true

[Install]
WantedBy=timers.target
EOF

systemctl enable certbot-renew.timer
systemctl start certbot-renew.timer

# Generate identity key for discovery service (Ed25519)
mkdir -p /opt/ascii-chat/keys
ssh-keygen -t ed25519 -f /opt/ascii-chat/keys/discovery-service-identity.key -N "" -C "discovery-service"

# Optional: Create client whitelist (if you want to restrict access)
# echo "ssh-ed25519 AAAAC3Nza... user@host" > /opt/ascii-chat/keys/allowed-clients.pub

echo "✓ Discovery service deployment complete!"
echo "  - WebSocket: wss://discovery-service.ascii-chat.com:27227"
echo "  - TLS Layer: Let's Encrypt + BearSSL"
echo "  - App Crypto: libsodium (--key, --client-keys work)"
echo "  - Identity: /opt/ascii-chat/keys/discovery-service-identity.key"
echo "  - Status: systemctl status ascii-chat-discovery"
```

### DNS Configuration

**DNS Records:**
```
Type    Name                   Content/Target
----    ----                   --------------
CNAME   web.ascii-chat.com     cname.vercel-dns.com
A       discovery-service      <VPS IP address>
```

**Vercel Domain Settings:**
- Add custom domain `web.ascii-chat.com` in Vercel dashboard
- Vercel automatically provisions SSL certificate
- **SSL/TLS:** Full (strict) - automatic
- **Always Use HTTPS:** On - automatic
- **HTTP/2:** On - automatic
- **Brotli Compression:** On - automatic
- **WebSockets:** Supported (for future WebSocket connections)

**Note:** Point `web.ascii-chat.com` to `cname.vercel-dns.com` in your DNS provider, then add the domain in Vercel project settings.

### Environment Variables

**web/.env.production:**
```bash
# Discovery service (WebSocket with TLS)
VITE_DISCOVERY_WS_URL=wss://discovery-service.ascii-chat.com:27227

# Optional: TURN server for WebRTC NAT traversal
VITE_TURN_URL=turn:turn.ascii-chat.com:3478
VITE_TURN_USERNAME=ascii-chat
VITE_TURN_CREDENTIAL=<secret>
```

**Usage in TypeScript:**
```typescript
// src/config.ts
export const config = {
  apiUrl: import.meta.env.VITE_API_URL || 'http://localhost:8000',
  wsUrl: import.meta.env.VITE_WS_URL || 'ws://localhost:27226',
  acdsUrl: import.meta.env.VITE_ACDS_URL || 'http://localhost:27225',
  turnUrl: import.meta.env.VITE_TURN_URL,
  turnUsername: import.meta.env.VITE_TURN_USERNAME,
  turnCredential: import.meta.env.VITE_TURN_CREDENTIAL,
};
```

### Monitoring & Analytics

**Google Analytics (GA4):**
```html
<!-- index.html -->
<head>
  <!-- ... -->

  <!-- Google Analytics (GA4) -->
  <script async src="https://www.googletagmanager.com/gtag/js?id=G-XXXXXXXXXX"></script>
  <script>
    window.dataLayer = window.dataLayer || [];
    function gtag(){dataLayer.push(arguments);}
    gtag('js', new Date());
    gtag('config', 'G-XXXXXXXXXX', {
      page_title: 'ascii-chat Web',
      send_page_view: true
    });
  </script>
</head>
```

**Vercel Analytics (Web Vitals):**
```typescript
// src/main.ts
import { inject } from '@vercel/analytics';

// Initialize Vercel Analytics
inject();

// Track custom events
import { track } from '@vercel/analytics';

track('mirror_mode_started');
track('client_connected', { transport: 'websocket' });
track('ascii_render_fps', { fps: 30 });
```

**Install Vercel Analytics:**
```bash
cd web/
bun add @vercel/analytics
```

**Server-Side Monitoring:**
```bash
# Install Prometheus + Grafana (optional)
apt-get install -y prometheus grafana

# Or use simple log monitoring
journalctl -u ascii-chat-server -f --since "1 hour ago"
journalctl -u acds -f --since "1 hour ago"
```

---

## Step-by-Step Implementation Guide

This section provides an actionable, ordered checklist for implementing WebAssembly browser support. Focus is on **Mirror Mode MVP first** (webcam → WASM ASCII rendering), then **Client Mode** (WebSocket → server frames).

### Phase 0: Project Scaffolding & Setup

**Directory Structure:**
```bash
# Create web frontend directory
mkdir -p web/{src/{pages,components,lib,wasm},public/{wasm,fonts}}
cd web/

# Initialize Bun + Vite project
bun create vite . --template vanilla-ts
bun install

# Add core dependencies
bun add -D vite @vitejs/plugin-legacy tailwindcss postcss autoprefixer
bun add -D @types/node
bun add xterm @xterm/addon-fit @xterm/addon-web-links
bun add @vercel/analytics

# Initialize Tailwind CSS
bunx tailwindcss init -p
```

**Tailwind Configuration (tailwind.config.js):**
```bash
cat > tailwind.config.js << 'EOF'
/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{js,ts,jsx,tsx}'],
  theme: {
    extend: {
      colors: {
        terminal: {
          bg: '#0c0c0c',
          fg: '#cccccc',
          black: '#0c0c0c',
          red: '#c50f1f',
          green: '#13a10e',
          yellow: '#c19c00',
          blue: '#0037da',
          magenta: '#881798',
          cyan: '#3a96dd',
          white: '#cccccc',
          brightBlack: '#767676',
          brightRed: '#e74856',
          brightGreen: '#16c60c',
          brightYellow: '#f9f1a5',
          brightBlue: '#3b78ff',
          brightMagenta: '#b4009e',
          brightCyan: '#61d6d6',
          brightWhite: '#f2f2f2',
        },
      },
      fontFamily: {
        mono: ['JetBrains Mono', 'Cascadia Code', 'Fira Code', 'monospace'],
      },
    },
  },
  plugins: [],
}
EOF
```

**Base HTML with OG Meta Tags (index.html):**
```bash
cat > index.html << 'EOF'
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />

  <!-- Primary Meta Tags -->
  <title>ascii-chat | Terminal Video Chat in Your Browser</title>
  <meta name="title" content="ascii-chat | Terminal Video Chat in Your Browser" />
  <meta name="description" content="Real-time video chat with ASCII art rendering, end-to-end encryption, and WebRTC support. Experience the terminal aesthetic in your browser." />

  <!-- Open Graph / Facebook -->
  <meta property="og:type" content="website" />
  <meta property="og:url" content="https://web.ascii-chat.com/" />
  <meta property="og:title" content="ascii-chat | Terminal Video Chat in Your Browser" />
  <meta property="og:description" content="Real-time video chat with ASCII art rendering, end-to-end encryption, and WebRTC support." />
  <meta property="og:image" content="https://web.ascii-chat.com/og-image.png" />

  <!-- Twitter -->
  <meta property="twitter:card" content="summary_large_image" />
  <meta property="twitter:url" content="https://web.ascii-chat.com/" />
  <meta property="twitter:title" content="ascii-chat | Terminal Video Chat in Your Browser" />
  <meta property="twitter:description" content="Real-time video chat with ASCII art rendering, end-to-end encryption, and WebRTC support." />
  <meta property="twitter:image" content="https://web.ascii-chat.com/og-image.png" />

  <!-- Favicon -->
  <link rel="icon" type="image/svg+xml" href="/favicon.svg" />

  <!-- Preload critical WASM -->
  <link rel="modulepreload" href="/wasm/mirror.js" />
  <link rel="preload" href="/wasm/mirror.wasm" as="fetch" type="application/wasm" crossorigin />

  <!-- Google Analytics (GA4) -->
  <script async src="https://www.googletagmanager.com/gtag/js?id=G-XXXXXXXXXX"></script>
  <script>
    window.dataLayer = window.dataLayer || [];
    function gtag(){dataLayer.push(arguments);}
    gtag('js', new Date());
    gtag('config', 'G-XXXXXXXXXX');
  </script>
</head>
<body class="bg-terminal-bg text-terminal-fg font-mono">
  <div id="app"></div>
  <script type="module" src="/src/main.ts"></script>
</body>
</html>
EOF
```

**Sitemap (public/sitemap.xml):**
```bash
cat > public/sitemap.xml << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
  <url>
    <loc>https://web.ascii-chat.com/</loc>
    <lastmod>2024-01-01</lastmod>
    <changefreq>weekly</changefreq>
    <priority>1.0</priority>
  </url>
  <url>
    <loc>https://web.ascii-chat.com/mirror</loc>
    <lastmod>2024-01-01</lastmod>
    <changefreq>monthly</changefreq>
    <priority>0.8</priority>
  </url>
  <url>
    <loc>https://web.ascii-chat.com/client</loc>
    <lastmod>2024-01-01</lastmod>
    <changefreq>monthly</changefreq>
    <priority>0.8</priority>
  </url>
  <url>
    <loc>https://web.ascii-chat.com/discovery</loc>
    <lastmod>2024-01-01</lastmod>
    <changefreq>monthly</changefreq>
    <priority>0.8</priority>
  </url>
</urlset>
EOF
```

**Robots.txt (public/robots.txt):**
```bash
cat > public/robots.txt << 'EOF'
User-agent: *
Allow: /
Sitemap: https://web.ascii-chat.com/sitemap.xml
EOF
```

**Vite Configuration (vite.config.ts):**
```typescript
import { defineConfig } from 'vite'
import path from 'path'

export default defineConfig({
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
      '@wasm': path.resolve(__dirname, './public/wasm'),
    },
  },
  server: {
    port: 3000,
    headers: {
      'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cross-Origin-Opener-Policy': 'same-origin',
    },
  },
  build: {
    target: 'es2020',
    minify: 'terser',
    rollupOptions: {
      output: {
        manualChunks: {
          'wasm-loader': ['./src/wasm/loader.ts'],
          'xterm': ['xterm', '@xterm/addon-fit'],
        },
      },
    },
  },
  optimizeDeps: {
    exclude: ['@ffmpeg/ffmpeg', '@ffmpeg/util'],
  },
})
```

---

### Phase 1: Mirror Mode MVP (Webcam → WASM ASCII Rendering)

**Step 1.1: Set up Emscripten build system**

```bash
# Install Emscripten SDK
cd ~/
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# Return to project
cd ~/ascii-chat/

# Create Emscripten toolchain file
cat > cmake/Emscripten.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Emscripten)
set(CMAKE_C_COMPILER emcc)
set(CMAKE_CXX_COMPILER em++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
```

**Step 1.2: Create WASM-specific ASCII rendering module**

```bash
# Create WASM-specific source file
cat > lib/video/ascii_wasm.c << 'EOF'
// lib/video/ascii_wasm.c
#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include "ascii.h"

// Exported function: Convert RGB frame to ASCII
EMSCRIPTEN_KEEPALIVE
char* convert_frame_to_ascii(
    uint8_t* rgb_data,
    int width,
    int height,
    int ascii_width,
    int ascii_height
) {
    // Allocate output buffer (ascii_width * ascii_height + 1 for null terminator)
    char* output = (char*)malloc(ascii_width * ascii_height + 1);
    if (!output) return NULL;

    // Call existing ascii_render_frame_direct()
    asciichat_error_t result = ascii_render_frame_direct(
        rgb_data, width, height,
        output, ascii_width, ascii_height
    );

    if (result != ASCIICHAT_OK) {
        free(output);
        return NULL;
    }

    output[ascii_width * ascii_height] = '\0';
    return output;
}

// Free memory allocated by convert_frame_to_ascii
EMSCRIPTEN_KEEPALIVE
void free_ascii_buffer(char* buffer) {
    free(buffer);
}
EOF
```

**Step 1.3: Create CMake target for Mirror Mode WASM**

```bash
# Add to CMakeLists.txt (or create build-wasm/CMakeLists.txt)
cat >> CMakeLists.txt << 'EOF'

# WebAssembly Mirror Mode Target
if(EMSCRIPTEN)
  add_executable(mirror_wasm
    lib/video/ascii_wasm.c
    lib/video/ascii.c
    lib/video/simd.c
  )

  target_include_directories(mirror_wasm PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/video
    ${CMAKE_SOURCE_DIR}/lib/log
  )

  set_target_properties(mirror_wasm PROPERTIES
    LINK_FLAGS "\
      -s MODULARIZE=1 \
      -s EXPORT_NAME='MirrorModule' \
      -s EXPORTED_FUNCTIONS='[\"_convert_frame_to_ascii\",\"_free_ascii_buffer\"]' \
      -s EXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\",\"getValue\",\"setValue\"]' \
      -s ALLOW_MEMORY_GROWTH=1 \
      -s INITIAL_MEMORY=16MB \
      -s MAXIMUM_MEMORY=256MB \
      -s WASM_BIGINT=1 \
      -s SIMD=1 \
      -msimd128 \
      -O3 \
      --no-entry"
  )

  # Copy outputs to web/public/wasm/
  add_custom_command(TARGET mirror_wasm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      ${CMAKE_BINARY_DIR}/mirror_wasm.js
      ${CMAKE_BINARY_DIR}/mirror_wasm.wasm
      ${CMAKE_SOURCE_DIR}/web/public/wasm/
  )
endif()
EOF
```

**Step 1.4: Build Mirror Mode WASM**

```bash
# Configure and build
source ~/emsdk/emsdk_env.sh
cmake -B build-wasm -DCMAKE_TOOLCHAIN_FILE=cmake/Emscripten.cmake
cmake --build build-wasm --target mirror_wasm

# Verify outputs
ls -lh build-wasm/mirror_wasm.{js,wasm}
ls -lh web/public/wasm/mirror_wasm.{js,wasm}
```

**Step 1.5: Create WASM Loader (TypeScript)**

```typescript
// web/src/wasm/mirror-loader.ts
export interface MirrorWASM {
  convertFrameToAscii(
    rgbData: Uint8Array,
    width: number,
    height: number,
    asciiWidth: number,
    asciiHeight: number
  ): string | null;
  ready: Promise<void>;
}

export async function loadMirrorWASM(): Promise<MirrorWASM> {
  // @ts-ignore - Emscripten module
  const MirrorModule = await import('/wasm/mirror_wasm.js');

  const module = await MirrorModule.default();

  const convertPtr = module.cwrap('convert_frame_to_ascii', 'number', [
    'number', 'number', 'number', 'number', 'number'
  ]);
  const freePtr = module.cwrap('free_ascii_buffer', null, ['number']);

  return {
    convertFrameToAscii(rgbData, width, height, asciiWidth, asciiHeight) {
      // Allocate memory in WASM heap
      const dataPtr = module._malloc(rgbData.length);
      module.HEAPU8.set(rgbData, dataPtr);

      // Call WASM function
      const resultPtr = convertPtr(dataPtr, width, height, asciiWidth, asciiHeight);

      // Free input buffer
      module._free(dataPtr);

      if (resultPtr === 0) return null;

      // Read string from WASM heap
      const result = module.UTF8ToString(resultPtr);

      // Free output buffer
      freePtr(resultPtr);

      return result;
    },
    ready: Promise.resolve(),
  };
}
```

**Step 1.6: Implement Mirror Mode UI**

```typescript
// web/src/pages/Mirror.ts
import { loadMirrorWASM, type MirrorWASM } from '@/wasm/mirror-loader';
import { Terminal } from 'xterm';
import { FitAddon } from '@xterm/addon-fit';
import 'xterm/css/xterm.css';

export class MirrorPage {
  private wasm: MirrorWASM | null = null;
  private terminal: Terminal;
  private fitAddon: FitAddon;
  private videoElement: HTMLVideoElement;
  private canvasElement: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private animationFrameId: number | null = null;

  constructor(private container: HTMLElement) {
    this.terminal = new Terminal({
      theme: {
        background: '#0c0c0c',
        foreground: '#cccccc',
      },
      fontFamily: 'JetBrains Mono, monospace',
      fontSize: 14,
      cursorBlink: false,
    });

    this.fitAddon = new FitAddon();
    this.terminal.loadAddon(this.fitAddon);

    this.videoElement = document.createElement('video');
    this.videoElement.autoplay = true;
    this.videoElement.style.display = 'none';

    this.canvasElement = document.createElement('canvas');
    this.canvasElement.style.display = 'none';
    this.ctx = this.canvasElement.getContext('2d')!;
  }

  async init() {
    // Create UI
    this.container.innerHTML = `
      <div class="h-screen flex flex-col p-4">
        <div class="mb-4">
          <h1 class="text-2xl font-bold text-terminal-cyan">ascii-chat | Mirror Mode</h1>
          <p class="text-terminal-fg opacity-70">Webcam → ASCII rendering in real-time</p>
        </div>
        <div id="terminal-container" class="flex-1 bg-terminal-bg border border-terminal-brightBlack"></div>
      </div>
    `;

    // Initialize terminal
    const terminalContainer = this.container.querySelector('#terminal-container')!;
    this.terminal.open(terminalContainer as HTMLElement);
    this.fitAddon.fit();

    // Load WASM
    this.terminal.writeln('Loading WASM module...');
    this.wasm = await loadMirrorWASM();
    this.terminal.writeln('✓ WASM loaded');

    // Start webcam
    this.terminal.writeln('Requesting webcam access...');
    const stream = await navigator.mediaDevices.getUserMedia({
      video: { width: 640, height: 480, frameRate: 30 }
    });
    this.videoElement.srcObject = stream;
    this.terminal.writeln('✓ Webcam active');

    // Wait for video metadata
    await new Promise(resolve => {
      this.videoElement.onloadedmetadata = resolve;
    });

    // Set canvas size to match video
    this.canvasElement.width = this.videoElement.videoWidth;
    this.canvasElement.height = this.videoElement.videoHeight;

    // Start rendering loop
    this.terminal.clear();
    this.startRenderLoop();
  }

  private startRenderLoop() {
    const render = () => {
      if (!this.wasm) return;

      // Draw video frame to canvas
      this.ctx.drawImage(
        this.videoElement,
        0, 0,
        this.canvasElement.width,
        this.canvasElement.height
      );

      // Get RGB data
      const imageData = this.ctx.getImageData(
        0, 0,
        this.canvasElement.width,
        this.canvasElement.height
      );

      // Extract RGB (skip alpha channel)
      const rgbData = new Uint8Array(imageData.width * imageData.height * 3);
      for (let i = 0; i < imageData.data.length; i += 4) {
        const j = (i / 4) * 3;
        rgbData[j] = imageData.data[i];       // R
        rgbData[j + 1] = imageData.data[i + 1]; // G
        rgbData[j + 2] = imageData.data[i + 2]; // B
      }

      // Convert to ASCII via WASM
      const asciiWidth = this.terminal.cols;
      const asciiHeight = this.terminal.rows;
      const ascii = this.wasm.convertFrameToAscii(
        rgbData,
        this.canvasElement.width,
        this.canvasElement.height,
        asciiWidth,
        asciiHeight
      );

      if (ascii) {
        // Clear terminal and write ASCII frame
        this.terminal.clear();
        this.terminal.write(ascii);
      }

      // Schedule next frame
      this.animationFrameId = requestAnimationFrame(render);
    };

    render();
  }

  destroy() {
    if (this.animationFrameId) {
      cancelAnimationFrame(this.animationFrameId);
    }

    if (this.videoElement.srcObject) {
      (this.videoElement.srcObject as MediaStream)
        .getTracks()
        .forEach(track => track.stop());
    }

    this.terminal.dispose();
  }
}
```

**Step 1.7: Create main entry point**

```typescript
// web/src/main.ts
import './style.css';
import { inject } from '@vercel/analytics';
import { MirrorPage } from './pages/Mirror';

// Initialize Vercel Analytics
inject();

// Simple router
const app = document.querySelector<HTMLDivElement>('#app')!;

const route = window.location.pathname;

if (route === '/mirror' || route === '/') {
  const mirror = new MirrorPage(app);
  mirror.init();
} else {
  app.innerHTML = `
    <div class="h-screen flex items-center justify-center">
      <div class="text-center">
        <h1 class="text-4xl font-bold text-terminal-cyan mb-4">ascii-chat</h1>
        <p class="text-terminal-fg mb-8">Choose a mode:</p>
        <div class="space-x-4">
          <a href="/mirror" class="px-4 py-2 bg-terminal-cyan text-terminal-bg rounded hover:opacity-80">Mirror Mode</a>
          <a href="/client" class="px-4 py-2 bg-terminal-green text-terminal-bg rounded hover:opacity-80">Client Mode</a>
        </div>
      </div>
    </div>
  `;
}
```

**Step 1.8: Test Mirror Mode MVP**

```bash
cd web/
bun run dev

# Open browser to http://localhost:3000/mirror
# Expected: Webcam feed converted to ASCII in terminal display
```

---

### Phase 2: Client Mode (WebSocket → Server ASCII Frames)

**Step 2.1: Compile crypto handshake to WASM (with libsodium)**

**Important:** libsodium must be compiled to WASM along with our crypto code. Emscripten provides libsodium via its port system.

```bash
# Create WASM wrapper for crypto
cat > lib/crypto/crypto_wasm.c << 'EOF'
// lib/crypto/crypto_wasm.c
#include <emscripten.h>
#include <sodium.h>  // Emscripten will provide this via -sUSE_LIBSODIUM=1
#include "handshake/client.h"
#include "handshake/server.h"

// Export crypto_client_init, crypto_client_send_hello, etc.
EMSCRIPTEN_KEEPALIVE
crypto_client_state_t* crypto_client_create(void) {
    // Initialize libsodium (safe to call multiple times)
    if (sodium_init() < 0) {
        return NULL;
    }

    crypto_client_state_t* state = malloc(sizeof(crypto_client_state_t));
    crypto_client_init(state);
    return state;
}

EMSCRIPTEN_KEEPALIVE
int crypto_client_generate_hello(
    crypto_client_state_t* state,
    uint8_t* output,
    size_t* output_len
) {
    return crypto_client_send_hello(state, output, output_len);
}

EMSCRIPTEN_KEEPALIVE
int crypto_client_process_response(
    crypto_client_state_t* state,
    const uint8_t* server_response,
    size_t response_len
) {
    return crypto_client_handle_response(state, server_response, response_len);
}

EMSCRIPTEN_KEEPALIVE
void crypto_client_destroy(crypto_client_state_t* state) {
    crypto_client_cleanup(state);
    free(state);
}
EOF

# Add to CMake
cat >> CMakeLists.txt << 'EOF'

# WebAssembly Client Mode Target (with crypto + libsodium)
if(EMSCRIPTEN)
  add_executable(client_wasm
    lib/crypto/crypto_wasm.c
    lib/crypto/handshake/client.c
    lib/crypto/handshake/common.c
    lib/crypto/crypto.c
    lib/video/ascii_wasm.c
    lib/video/ascii.c
  )

  target_include_directories(client_wasm PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/crypto
    ${CMAKE_SOURCE_DIR}/lib/video
    ${CMAKE_SOURCE_DIR}/include
  )

  # CRITICAL: Use Emscripten's libsodium port
  # This compiles libsodium to WASM and links it with our code
  set_target_properties(client_wasm PROPERTIES
    LINK_FLAGS "\
      -sUSE_LIBSODIUM=1 \
      -s MODULARIZE=1 \
      -s EXPORT_NAME='ClientModule' \
      -s EXPORTED_FUNCTIONS='[\"_crypto_client_create\",\"_crypto_client_generate_hello\",\"_crypto_client_process_response\",\"_crypto_client_destroy\"]' \
      -s EXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\",\"getValue\",\"setValue\"]' \
      -s ALLOW_MEMORY_GROWTH=1 \
      -s INITIAL_MEMORY=32MB \
      -s MAXIMUM_MEMORY=256MB \
      -O3 \
      --no-entry"
  )

  add_custom_command(TARGET client_wasm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      ${CMAKE_BINARY_DIR}/client_wasm.js
      ${CMAKE_BINARY_DIR}/client_wasm.wasm
      ${CMAKE_SOURCE_DIR}/web/public/wasm/
  )
endif()
EOF

# Build (Emscripten will automatically download and compile libsodium)
cmake --build build-wasm --target client_wasm

# Verify libsodium was included
ls -lh build-wasm/client_wasm.wasm
# Expected: ~500KB+ (includes libsodium WASM)
```

**How `-sUSE_LIBSODIUM=1` Works:**

1. Emscripten downloads libsodium source (first build only)
2. Compiles libsodium to WASM with optimizations
3. Links libsodium functions into `client_wasm.wasm`
4. Our C crypto code calls libsodium functions normally
5. Everything runs in the same WASM module (no JS glue needed)

**Verification:**
```bash
# Check that libsodium symbols are in the WASM
wasm-objdump -x build-wasm/client_wasm.wasm | grep sodium_init
# Should show: sodium_init (function)
```

**Alternative: libsodium.js (NOT RECOMMENDED)**

You could use libsodium.js (pre-compiled WASM) instead:

```typescript
// Separate library approach
import sodium from 'libsodium-wrappers';
await sodium.ready;

// Then manually bridge between libsodium.js and your WASM crypto code
const publicKey = sodium.crypto_box_keypair();
```

**Why NOT to use libsodium.js:**
- **Two WASM modules**: Your crypto code + separate libsodium.wasm (~175KB)
- **JS bridge overhead**: Have to marshal data between two WASM modules via JavaScript
- **API mismatch**: libsodium.js has a different API than C libsodium (async, promises)
- **Larger bundle**: libsodium.js includes full library, Emscripten tree-shakes to only what you use

**Why Emscripten's `-sUSE_LIBSODIUM=1` is better:**
- **Single WASM module**: All code compiled together, direct function calls (no JS bridge)
- **Smaller binary**: Only includes libsodium functions you actually call (~100KB typical)
- **Zero-cost abstraction**: C crypto code works identically in WASM as native
- **Type safety**: No marshalling between JS types and C types

**Step 2.2: Implement WebSocket transport (TypeScript)**

```typescript
// web/src/lib/transport.ts
import { loadClientWASM } from '@/wasm/client-loader';

export class WebSocketTransport {
  private ws: WebSocket | null = null;
  private cryptoWasm: any = null;
  private cryptoState: any = null;

  constructor(private url: string) {}

  async connect(): Promise<void> {
    // Load crypto WASM
    this.cryptoWasm = await loadClientWASM();
    this.cryptoState = this.cryptoWasm.createClient();

    // Open WebSocket
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.url);
      this.ws.binaryType = 'arraybuffer';

      this.ws.onopen = async () => {
        // Send crypto handshake
        const hello = this.cryptoWasm.generateHello(this.cryptoState);
        this.ws!.send(hello);
        resolve();
      };

      this.ws.onerror = (err) => reject(err);
    });
  }

  onMessage(callback: (data: ArrayBuffer) => void): void {
    if (!this.ws) throw new Error('Not connected');

    this.ws.onmessage = (event) => {
      const data = new Uint8Array(event.data);

      // Process through crypto layer
      const decrypted = this.cryptoWasm.decrypt(this.cryptoState, data);
      callback(decrypted.buffer);
    };
  }

  send(data: Uint8Array): void {
    if (!this.ws) throw new Error('Not connected');

    // Encrypt through crypto layer
    const encrypted = this.cryptoWasm.encrypt(this.cryptoState, data);
    this.ws.send(encrypted);
  }

  close(): void {
    this.ws?.close();
    if (this.cryptoState) {
      this.cryptoWasm.destroyClient(this.cryptoState);
    }
  }
}
```

**Step 2.3: Implement Client Mode UI**

```typescript
// web/src/pages/Client.ts
import { Terminal } from 'xterm';
import { FitAddon } from '@xterm/addon-fit';
import { WebSocketTransport } from '@/lib/transport';

export class ClientPage {
  private terminal: Terminal;
  private fitAddon: FitAddon;
  private transport: WebSocketTransport | null = null;

  constructor(private container: HTMLElement) {
    this.terminal = new Terminal({
      theme: { background: '#0c0c0c', foreground: '#cccccc' },
      fontFamily: 'JetBrains Mono, monospace',
      fontSize: 14,
    });

    this.fitAddon = new FitAddon();
    this.terminal.loadAddon(this.fitAddon);
  }

  async init() {
    this.container.innerHTML = `
      <div class="h-screen flex flex-col p-4">
        <div class="mb-4">
          <h1 class="text-2xl font-bold text-terminal-green">ascii-chat | Client Mode</h1>
          <input
            id="server-url"
            type="text"
            placeholder="ws://localhost:27226"
            class="mt-2 px-4 py-2 bg-terminal-black text-terminal-fg border border-terminal-brightBlack rounded w-96"
          />
          <button
            id="connect-btn"
            class="ml-2 px-4 py-2 bg-terminal-green text-terminal-bg rounded hover:opacity-80"
          >Connect</button>
        </div>
        <div id="terminal-container" class="flex-1 bg-terminal-bg border border-terminal-brightBlack"></div>
      </div>
    `;

    this.terminal.open(this.container.querySelector('#terminal-container') as HTMLElement);
    this.fitAddon.fit();

    // Connect button handler
    this.container.querySelector('#connect-btn')!.addEventListener('click', () => {
      const url = (this.container.querySelector('#server-url') as HTMLInputElement).value;
      this.connect(url);
    });
  }

  private async connect(url: string) {
    this.terminal.writeln(`Connecting to ${url}...`);

    try {
      this.transport = new WebSocketTransport(url);
      await this.transport.connect();

      this.terminal.writeln('✓ Connected');
      this.terminal.writeln('✓ Crypto handshake complete');
      this.terminal.clear();

      // Listen for ASCII frames from server
      this.transport.onMessage((data) => {
        const ascii = new TextDecoder().decode(data);
        this.terminal.clear();
        this.terminal.write(ascii);
      });

    } catch (err) {
      this.terminal.writeln(`✗ Connection failed: ${err}`);
    }
  }

  destroy() {
    this.transport?.close();
    this.terminal.dispose();
  }
}
```

**Step 2.4: Enable WebSocket support in libdatachannel**

```bash
# Edit cmake/dependencies/Libdatachannel.cmake
# Change line 252 from:
#   -DNO_WEBSOCKET=ON                     # Disable WebSocket support (we only need DataChannels)
# To:
#   -DNO_WEBSOCKET=OFF                    # Enable WebSocket support for browser clients

# Rebuild libdatachannel
rm -rf ~/.cache/ascii-chat/deps/libdatachannel-build/
cmake --build build
```

**Step 2.5: Implement server-side WebSocket handler (C) using libdatachannel**

```c
// lib/network/websocket/server.c
#include "platform/abstraction.h"
#include "network/acip/transport.h"
#include <rtc/rtc.h>  // libdatachannel WebSocket API

typedef struct websocket_transport_data {
    int ws_id;           // libdatachannel WebSocket ID
    bool is_tls;
    crypto_server_state_t crypto_state;
} websocket_transport_data_t;

static bool websocket_provides_encryption(acip_transport_t* transport) {
    websocket_transport_data_t* ws = (websocket_transport_data_t*)transport->impl_data;
    return ws->is_tls; // If wss://, TLS encrypts
}

static asciichat_error_t websocket_send(
    acip_transport_t* transport,
    const uint8_t* data,
    size_t len
) {
    websocket_transport_data_t* ws = (websocket_transport_data_t*)transport->impl_data;

    if (transport->methods->provides_encryption(transport)) {
        // TLS encrypts, send raw
        rtcSendMessage(ws->ws_id, (const char*)data, (int)len);
    } else {
        // No TLS, encrypt with libsodium
        uint8_t encrypted[len + crypto_secretbox_MACBYTES];
        crypto_encrypt_packet(&ws->crypto_state, data, len, encrypted);
        rtcSendMessage(ws->ws_id, (const char*)encrypted, (int)(len + crypto_secretbox_MACBYTES));
    }

    return ASCIICHAT_OK;
}

static acip_transport_methods_t websocket_methods = {
    .provides_encryption = websocket_provides_encryption,
    .send = websocket_send,
    .recv = websocket_recv,
    .close = websocket_close,
};

// libdatachannel WebSocket callbacks
static void ws_on_open(int ws_id, void* user_ptr) {
    log_info("WebSocket client connected (ws_id=%d)", ws_id);

    // Create transport wrapper
    acip_transport_t* transport = SAFE_MALLOC(sizeof(acip_transport_t), acip_transport_t*);
    websocket_transport_data_t* ws_data = SAFE_CALLOC(1, sizeof(websocket_transport_data_t), websocket_transport_data_t*);

    ws_data->ws_id = ws_id;
    ws_data->is_tls = (bool)(intptr_t)user_ptr; // TLS flag passed via user_ptr
    crypto_server_init(&ws_data->crypto_state);

    transport->impl_data = ws_data;
    transport->methods = &websocket_methods;

    rtcSetUserPointer(ws_id, transport);
}

static void ws_on_message(int ws_id, const char* data, int size, void* user_ptr) {
    acip_transport_t* transport = (acip_transport_t*)rtcGetUserPointer(ws_id);

    // Handle received packet
    handle_acip_packet(transport, (const uint8_t*)data, (size_t)size);
}

static void ws_on_closed(int ws_id, void* user_ptr) {
    log_info("WebSocket client disconnected (ws_id=%d)", ws_id);

    acip_transport_t* transport = (acip_transport_t*)rtcGetUserPointer(ws_id);
    if (transport) {
        SAFE_FREE(transport->impl_data);
        SAFE_FREE(transport);
    }
}

static void ws_on_error(int ws_id, const char* error, void* user_ptr) {
    log_error("WebSocket error (ws_id=%d): %s", ws_id, error);
}
```

**Step 2.6: Add WebSocket server to ascii-chat server mode**

```c
// src/server/websocket.c
#include <rtc/rtc.h>  // libdatachannel

int websocket_server_init(uint16_t port, bool use_tls) {
    // Initialize libdatachannel logging
    rtcInitLogger(RTC_LOG_INFO, NULL);

    // Create WebSocket server configuration
    rtcWsServerConfiguration config = {0};
    config.port = port;
    config.enableTls = use_tls;

    if (use_tls) {
        // Load TLS certificate (Let's Encrypt)
        config.certificatePemFile = "/etc/letsencrypt/live/discovery-service.ascii-chat.com/fullchain.pem";
        config.keyPemFile = "/etc/letsencrypt/live/discovery-service.ascii-chat.com/privkey.pem";
    }

    // Create WebSocket server
    int ws_server = rtcCreateWebSocketServer(&config, ws_on_client);
    if (ws_server < 0) {
        log_error("Failed to create WebSocket server");
        return -1;
    }

    log_info("WebSocket server listening on port %d (TLS: %s)", port, use_tls ? "yes" : "no");

    // Keep server running
    // libdatachannel runs callbacks in its own thread
    while (1) {
        sleep(1);
    }

    rtcDeleteWebSocketServer(ws_server);
    return 0;
}

// Client connection callback
static void ws_on_client(int ws_server, int ws_id, void* user_ptr) {
    // Set callbacks for this client connection
    rtcSetOpenCallback(ws_id, ws_on_open);
    rtcSetMessageCallback(ws_id, ws_on_message);
    rtcSetClosedCallback(ws_id, ws_on_closed);
    rtcSetErrorCallback(ws_id, ws_on_error);
}
```

**Step 2.7: Update server main.c to start WebSocket server**

```c
// src/server/main.c
if (options->websocket_port > 0) {
    log_info("Starting WebSocket server on port %d", options->websocket_port);

    // Determine if we should use TLS (if running as discovery-service with Let's Encrypt)
    bool use_tls = options->discovery_service && options->use_letsencrypt;

    // Spawn WebSocket thread
    asciichat_thread_t ws_thread;
    websocket_server_args_t* args = SAFE_MALLOC(sizeof(websocket_server_args_t), websocket_server_args_t*);
    args->port = options->websocket_port;
    args->use_tls = use_tls;

    asciichat_thread_create(&ws_thread, websocket_server_thread, args);
}
```

**Step 2.8: Test Client Mode end-to-end**

```bash
# Terminal 1: Start server with WebSocket support
./build/bin/ascii-chat server --websocket-port 27226

# Terminal 2: Start web dev server
cd web/
bun run dev

# Browser: Open http://localhost:3000/client
# Enter: ws://localhost:27226
# Click Connect
# Expected: Terminal shows ASCII frames from server
```

---

### Phase 3: Production Deployment

**Step 3.1: Build production assets**

```bash
cd web/
bun run build

# Verify output
ls -lh dist/
ls -lh dist/wasm/
```

**Step 3.2: Deploy to Vercel**

```bash
# Install Vercel CLI
bun add -g vercel

# Login
vercel login

# Deploy
vercel --prod

# Set custom domain in Vercel dashboard
# Add DNS CNAME: web.ascii-chat.com → cname.vercel-dns.com
```

**Step 3.3: Deploy discovery service to VPS**

```bash
# SSH to VPS
ssh user@discovery-service.ascii-chat.com

# Clone repo
git clone https://github.com/zfogg/ascii-chat.git
cd ascii-chat/

# Build with BearSSL + Let's Encrypt support
cmake -B build -DCMAKE_BUILD_TYPE=Release -DWITH_BEARSSL=ON
cmake --build build --target ascii-chat

# Set up systemd service (see Deployment section above)
sudo systemctl start ascii-chat-discovery
sudo systemctl enable ascii-chat-discovery
```

**Step 3.4: Test production deployment**

```bash
# Test Mirror Mode
open https://web.ascii-chat.com/mirror

# Test Client Mode
open https://web.ascii-chat.com/client
# Connect to: wss://discovery-service.ascii-chat.com:27227
```

---

## Conclusion

This implementation plan provides a comprehensive roadmap for bringing ascii-chat to the browser using WebAssembly. Key takeaways:

1. **Maximize Code Reuse** - ~60-70% of C code can be compiled to WASM (crypto, ASCII conversion, protocol)
2. **Leverage Browser APIs** - Native browser features (getUserMedia, Web Audio, WebRTC) are often better than compiled equivalents
3. **Transport Abstraction** - The existing `acip_transport_t` design makes WebSocket/WebRTC integration straightforward
4. **Incremental Approach** - Start with Mirror mode, add complexity in phases
5. **Server Compatibility** - WebSocket server runs alongside TCP, no breaking changes for native clients

**Next Steps:**
1. Review this document with team
2. Set up Emscripten development environment
3. Begin Phase 1 (Mirror Mode) implementation
4. Iterate based on performance metrics and user feedback

**Questions for Discussion:**
- Should we support Safari's older getUserMedia API (navigator.getUserMedia)?
- Do we need TURN server for WebRTC, or is STUN sufficient?
- What's the minimum browser version we want to support?

---

## Quick Reference

### Development Commands (Bun + Vite)

```bash
# Project setup
cd web/
bun install                          # Install dependencies
bun run wasm:build                   # Build WASM modules
bun run dev                          # Start dev server (http://localhost:3000)

# Development
bun run type-check                   # TypeScript type checking
bun run lint                         # ESLint
bun run build                        # Production build
bun run preview                      # Preview production build

# WASM workflow
bun run wasm:build                   # Build C → WASM
bun run wasm:copy                    # Copy to public/wasm/
bun run prebuild                     # Both (runs before build automatically)

# Deployment
vercel --prod                        # Deploy to Vercel
```

### Project URLs

**Production:**
- Frontend: https://web.ascii-chat.com
- Discovery Service: wss://discovery-service.ascii-chat.com:27227
- User Servers: User-specified (e.g., ws://192.168.1.100:27226)

**Development:**
- Frontend: http://localhost:3000
- Discovery Service: ws://localhost:27227
- Test Server: ws://localhost:27226

### Key File Locations

```
ascii-chat/
├── web/                             # Frontend (Vite + Bun)
│   ├── src/main.ts                  # App entry point
│   ├── src/pages/Mirror.ts          # Mirror mode UI
│   ├── src/pages/Client.ts          # Client mode UI
│   ├── vite.config.ts               # Vite configuration
│   └── tailwind.config.js           # Tailwind CSS config
├── lib/
│   ├── video/ascii.c                # ASCII conversion (WASM)
│   ├── crypto/handshake/*.c         # Crypto handshake (WASM)
│   └── network/websocket/server.c   # WebSocket server (NEW)
├── build-wasm/                      # WASM build output
│   ├── mirror.js                    # Emscripten glue
│   ├── mirror.wasm                  # WASM binary
│   ├── client.js                    # Emscripten glue
│   └── client.wasm                  # WASM binary
└── notes/
    └── webassembly-implementation-plan.md  # This document
```

### Tech Stack Summary

| Layer | Technology | Purpose |
|-------|-----------|---------|
| **Build Tool** | Vite | Fast dev server, optimized production builds |
| **Package Manager** | Bun | 3x faster than npm, TypeScript native |
| **CSS Framework** | Tailwind CSS | Utility-first styling, terminal theme |
| **Terminal Emulator** | xterm.js | ANSI sequence rendering, GPU acceleration |
| **WASM Compiler** | Emscripten | C/C++ → WebAssembly (SIMD support) |
| **Frontend Hosting** | Vercel | Global Edge Network, auto-deploy from GitHub |
| **Backend Hosting** | VPS (Hetzner/DO) | Persistent WebSocket, stateful connections |
| **Domain** | web.ascii-chat.com | Frontend (static) |
| **Domain** | discovery-service.ascii-chat.com | Discovery WebSocket (port 27227) |

### Browser Support Matrix

| Feature | Chrome | Firefox | Safari | Edge |
|---------|--------|---------|--------|------|
| WASM SIMD | 91+ | 89+ | 16.4+ | 91+ |
| getUserMedia | 53+ | 36+ | 11+ | 79+ |
| Web Audio API | 35+ | 25+ | 14.1+ | 79+ |
| WebSocket | 43+ | 48+ | 10.1+ | 14+ |
| WebRTC | 56+ | 44+ | 11+ | 79+ |
| ES2020 | 80+ | 74+ | 14+ | 80+ |

**Minimum Versions:**
- Chrome 91+ (June 2021)
- Firefox 89+ (June 2021)
- Safari 16.4+ (March 2023)
- Edge 91+ (May 2021)

### Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| **Initial Load** | <2s | Time to Interactive (TTI) |
| **WASM Load** | <500ms | mirror.wasm fetch + compile |
| **First Frame** | <100ms | getUserMedia → ASCII render |
| **Frame Rate** | 30 FPS | Sustained, 800x600 → 120x40 |
| **Audio Latency** | <200ms | Round-trip (capture → playback) |
| **WebSocket RTT** | <100ms | Ping-pong on good connection |
| **WebRTC Setup** | <3s | Offer → Answer → Connected |
| **Memory Usage** | <200MB | Peak during video chat (native decoder) |
| **Memory (ffmpeg)** | <500MB | Peak with ffmpeg.wasm loaded |
| **Bundle Size** | <500KB | Initial JS+CSS (gzipped, no ffmpeg) |
| **Bundle (ffmpeg)** | ~30MB | ffmpeg.wasm (lazy loaded on demand) |
| **WASM Size** | <2MB | mirror.wasm + client.wasm (gzipped) |

### Troubleshooting

**WASM fails to load:**
```bash
# Check MIME type
curl -I https://web.ascii-chat.com/wasm/mirror.wasm
# Should see: Content-Type: application/wasm
```

**WebSocket connection fails:**
```bash
# Test discovery service WebSocket endpoint
websocat wss://discovery-service.ascii-chat.com:27227
# Should connect without error

# Check discovery service logs
journalctl -u ascii-chat-discovery -f

# Test user-run server (example)
websocat ws://192.168.1.100:27226
```

**Bun install fails:**
```bash
# Clear cache
bun pm cache rm

# Force fresh install
rm -rf node_modules bun.lockb
bun install
```

**Vite build fails:**
```bash
# Check TypeScript errors
bun run type-check

# Clean and rebuild
rm -rf dist node_modules
bun install
bun run build
```

---

**Document Status:** Ready for Implementation
**Last Updated:** 2026-02-06
**Tech Stack:** Vite + Bun + Tailwind CSS + Emscripten
**Target Domain:** web.ascii-chat.com
