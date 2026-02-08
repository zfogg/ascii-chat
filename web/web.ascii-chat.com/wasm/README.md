# WebAssembly Client Mode Implementation

This directory contains the WebAssembly build configuration for ascii-chat, supporting both mirror mode (local webcam preview) and client mode (network connectivity with crypto).

## Overview

The WASM build compiles ascii-chat's C libraries to WebAssembly for browser execution:

- **mirror-web**: Mirror mode with video processing and ASCII conversion
- **client-web**: Client mode with crypto handshake, packet protocol, and network abstraction

## Build Targets

### From Main Build (Recommended)

```bash
# Build both mirror and client WASM modules
cmake --build build --target wasm

# Build only mirror mode
cmake --build build --target mirror-wasm

# Build only client mode
cmake --build build --target client-wasm
```

### From Web Directory

```bash
cd web/web.ascii-chat.com

# Build both modules
bun run wasm:build

# Build only mirror
bun run wasm:build:mirror

# Build only client
bun run wasm:build:client

# Watch mode (rebuild on file changes)
bun run wasm:watch

# Clean build artifacts
bun run wasm:clean
```

## Architecture

### Mirror Mode (`mirror-web`)

**Sources:**
- Entry point: `src/web/mirror.c`
- Video processing: `lib/video/**/*.c`
- Options system: `lib/options/**/*.c`
- Utilities: `lib/util/**/*.c`

**Features:**
- RGBA to ASCII conversion with SIMD optimization
- Color filters (grayscale, rainbow, etc.)
- Multiple render modes (foreground, background, half-block)
- Palette customization
- Matrix rain effect
- Real-time webcam processing

**Dependencies:**
- libsodium (crypto for logging)
- PCRE2 (regex for log filtering)

### Client Mode (`client-web`)

**Sources:**
- Entry point: `src/web/client.c`
- All mirror mode sources
- Crypto: `lib/crypto/**/*.c`
- Network protocol: `lib/network/packet_parsing.c`, `crc32.c`, `compression.c`

**Features:**
- X25519 key exchange
- XSalsa20-Poly1305 encryption/decryption
- Packet serialization/deserialization
- CRC32 validation
- Zstd compression
- Connection state management

**Dependencies:**
- libsodium (X25519, XSalsa20-Poly1305)
- PCRE2 (regex)
- Opus (audio codec - prepared but not yet integrated)
- zstd (compression)

## TypeScript Integration

### Mirror Mode

```typescript
import {
  initMirrorWasm,
  convertFrameToAscii,
  setColorFilter,
  ColorFilter
} from './src/wasm/mirror';

// Initialize
await initMirrorWasm({ width: 80, height: 40 });

// Convert video frame
const asciiOutput = convertFrameToAscii(rgbaData, 640, 480);

// Change settings
setColorFilter(ColorFilter.RAINBOW);
```

### Client Mode

```typescript
import {
  initClientWasm,
  generateKeypair,
  performHandshake,
  encryptPacket,
  decryptPacket,
  ConnectionState
} from './src/wasm/client';

// Initialize
await initClientWasm({ width: 80, height: 40 });

// Generate keypair
const publicKey = await generateKeypair();

// After receiving server's public key
setServerPublicKey(serverPubKeyHex);
performHandshake();

// Encrypt/decrypt packets
const ciphertext = encryptPacket(plaintext);
const plaintext = decryptPacket(ciphertext);
```

## Network Adapters

### WebSocket Bridge (`src/network/SocketBridge.ts`)

Replaces native BSD sockets with WebSocket API:

```typescript
const socket = new SocketBridge({
  url: 'wss://server.example.com:27224',
  onPacket: (packet) => {
    // Handle received packet
  }
});

await socket.connect();
socket.send(packetBytes);
```

### Client Connection Manager (`src/network/ClientConnection.ts`)

High-level abstraction combining WebSocket + WASM crypto:

```typescript
const client = new ClientConnection({
  serverUrl: 'wss://server.example.com:27224',
  width: 80,
  height: 40
});

client.onStateChange((state) => {
  console.log('Connection state:', state);
});

client.onPacketReceived((packet, payload) => {
  console.log('Received:', packet.type);
});

await client.connect(); // Initializes WASM, connects WebSocket, performs handshake
client.sendPacket(packetType, payload);
```

### Audio Pipeline (`src/audio/AudioPipeline.ts`)

Web Audio API integration for microphone capture and playback:

```typescript
const audio = new AudioPipeline({
  sampleRate: 48000,
  echoCancellation: true,
  onAudioData: (opusData) => {
    // Send encoded audio to server
  }
});

await audio.startCapture(); // Request microphone permission
await audio.playAudioData(opusData); // Play received audio
```

## Build Configuration

### CMakeLists.txt Structure

```cmake
# Dependencies (FetchContent)
- libsodium-cmake
- PCRE2
- Opus
- zstd

# Targets
- mirror-web: Mirror mode WASM module
- client-web: Client mode WASM module

# Exported Functions
- Mirror: _mirror_init_with_args, _mirror_convert_frame, etc.
- Client: _client_init_with_args, _client_generate_keypair, _client_encrypt_packet, etc.

# Emscripten Flags
- MODULARIZE=1, EXPORT_ES6=1 (ES6 module export)
- WASM=1 (WebAssembly output)
- ALLOW_MEMORY_GROWTH=1 (dynamic memory)
- INITIAL_MEMORY=32MB (mirror), 64MB (client)
- MAXIMUM_MEMORY=256MB (mirror), 512MB (client)
- WASM_BIGINT=1 (BigInt support)
- -msimd128 -mavx2 (SIMD optimization)
- -O3 (release optimization)
```

### Output Files

Generated files are copied to `src/wasm/dist/` for Vite import:

```
src/wasm/dist/
├── mirror.js         # Emscripten glue code (ES6 module)
├── mirror.wasm       # Compiled mirror mode
├── client.js         # Emscripten glue code (ES6 module)
└── client.wasm       # Compiled client mode
```

## Platform Abstraction

WASM-specific implementations in `lib/platform/wasm/`:

- `init.c` - Platform initialization (no-op for WASM)
- `terminal.c` - Terminal capability stubs
- `threading.c` - Single-threaded stubs (no pthreads)
- `system.c` - System functions (time, environment)
- `stubs/` - Stub implementations for unsupported features

Future additions for client mode:
- `network.c` - WebSocket bridge via EM_JS
- `storage.c` - localStorage bridge for config/keys

## Testing

### Unit Tests

```bash
cd web/web.ascii-chat.com
bun test tests/wasm/client.test.ts
```

Tests verify:
- WASM module initialization
- Keypair generation
- Connection state management
- Type safety of bindings

### Manual Testing

```bash
# Build WASM modules
cmake --build build --target wasm

# Start dev server
cd web/web.ascii-chat.com
bun run dev

# Open http://localhost:5173
```

## Implementation Status

### ✅ Completed

- [x] Enhanced CMakeLists.txt for client mode
- [x] Added libopus and zstd dependencies
- [x] Created client.c WASM entry point
- [x] Created client.ts TypeScript bindings
- [x] Created SocketBridge for WebSocket integration
- [x] Created ClientConnection high-level manager
- [x] Created AudioPipeline for Web Audio API
- [x] Updated build scripts in package.json
- [x] Updated main CMake wrapper targets

### 🚧 In Progress

- [ ] Complete crypto handshake protocol in ClientConnection
- [ ] Integrate Opus codec with WASM exports
- [ ] Implement video frame encoding/decoding
- [ ] Add WebRTC support for P2P connections

### 📋 TODO

- [ ] Create platform/wasm/network.c for WebSocket bridge
- [ ] Create platform/wasm/storage.c for localStorage
- [ ] Add Opus encoder/decoder WASM exports
- [ ] Write E2E tests with native server
- [ ] Add WebRTC data channel support
- [ ] Implement packet queue for async handling
- [ ] Add reconnection logic with exponential backoff

## Debugging

### Enable Console Logs

WASM modules log to browser console with `[C]` prefix:

```
[C] client_init_with_args: START
[C] platform_init OK
[C] options_init OK
[C] client_init_with_args: COMPLETE
```

### Memory Debugging

Set environment variable before running:

```bash
ASCII_CHAT_MEMORY_REPORT_BACKTRACE=1 bun run dev
```

### WASM Inspector

Use Chrome DevTools:
1. Open DevTools → Sources
2. Find `.wasm` files in file tree
3. Set breakpoints in WASM code
4. Inspect memory with Memory tab

## Resources

- [Emscripten Documentation](https://emscripten.org/docs/)
- [libsodium WASM](https://github.com/jedisct1/libsodium.js)
- [Web Audio API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Audio_API)
- [WebSocket API](https://developer.mozilla.org/en-US/docs/Web/API/WebSocket)
- [Opus Codec](https://opus-codec.org/)
