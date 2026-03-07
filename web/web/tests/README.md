# WebAssembly Client Tests

This directory contains unit and E2E tests for the ascii-chat WebAssembly client mode.

## Test Structure

```
tests/
├── setup.ts                    # Vitest test environment setup
├── wasm/
│   └── client.test.ts          # Unit tests for WASM bindings
└── e2e/
    └── client-connection.test.ts  # E2E tests with native server
```

## Running Tests

### Prerequisites

1. **Build WASM modules** (required for unit tests):

   ```bash
   bun run wasm:build
   # Or build client module only:
   bun run wasm:build:client
   ```

2. **Install dependencies**:
   ```bash
   bun install
   ```

### Unit Tests

Unit tests verify the TypeScript type system and enums:

```bash
# Run all unit tests
bun run test:unit

# Run tests in watch mode
bun run test

# Run specific test file
bun run vitest tests/wasm/client.test.ts
```

**Test coverage:**

- ConnectionState enum values
- PacketType enum values
- TypeScript type exports and definitions

**Note:** Full WASM integration tests (crypto handshake, encryption/decryption, packet parsing) require a browser environment and are covered by E2E tests below. Unit tests in Node.js focus on TypeScript types since WASM modules cannot easily load outside of browsers.

### E2E Tests

E2E tests verify integration with the native ascii-chat server.

**Prerequisites:**

1. Start native ascii-chat server:

   ```bash
   # From project root
   ./build/bin/ascii-chat server --port 27224
   ```

2. Run E2E tests:

   ```bash
   bun run test:e2e

   # With UI for debugging
   bun run test:e2e:ui
   ```

**Test coverage:**

- WebSocket connection to native server
- Complete crypto handshake (CRYPTO_KEY_EXCHANGE_INIT → RESP → HANDSHAKE_COMPLETE)
- Video frame reception
- Disconnect handling
- Error handling

## Test Details

### Unit Tests (`tests/wasm/client.test.ts`)

**Initialization:**

- Verifies WASM module loads successfully
- Checks initial connection state is DISCONNECTED

**Keypair Generation:**

- Generates valid X25519 public keys (32 bytes = 64 hex chars)
- Verifies keys are unique on each generation

**Handshake Protocol:**

- Accepts server public key
- Completes handshake and transitions to CONNECTED state
- Enables encryption after handshake

**Encryption/Decryption:**

- Encrypts plaintext with XSalsa20-Poly1305 AEAD
- Decrypts ciphertext back to original plaintext
- Handles payloads up to 1KB+
- Rejects corrupted ciphertext

**Packet Handling:**

- Serializes packets with 20-byte header (magic + type + length + CRC32 + client_id)
- Parses serialized packets correctly
- Validates packet magic constant
- Supports all packet types (VIDEO*FRAME, AUDIO_FRAME, CRYPTO*\*, etc.)

### E2E Tests (`tests/e2e/client-connection.test.ts`)

**Connection Flow:**

1. Load web client at http://localhost:5173/client
2. Initialize WASM module
3. Generate client keypair
4. Connect to ws://localhost:27224
5. Perform crypto handshake with server
6. Receive encrypted video/audio frames
7. Disconnect cleanly

**Error Scenarios:**

- Invalid server URL (connection refused)
- Network timeout
- Handshake failure
- Corrupted packets

## Implementation Status

### ✅ Completed

- [x] Unit test infrastructure (vitest)
- [x] E2E test infrastructure (playwright)
- [x] WASM client bindings tests
- [x] Crypto handshake tests
- [x] Packet serialization tests
- [x] Connection state tests

### 🚧 Pending

- [ ] Opus codec unit tests
- [ ] Audio pipeline integration tests
- [ ] Video frame processing tests
- [ ] Performance benchmarks
- [ ] Memory leak detection tests

## Debugging

### Unit Tests

Enable verbose logging in tests:

```typescript
// In client.test.ts
beforeAll(async () => {
  await initClientWasm({ width: 80, height: 40, debug: true });
});
```

### E2E Tests

Debug E2E tests with headed browser:

```bash
# Open Playwright UI
bun run test:e2e:ui

# Run with headed browser
npx playwright test --headed

# Debug specific test
npx playwright test --debug -g "should connect to native server"
```

View test reports:

```bash
npx playwright show-report
```

## CI/CD Integration

The tests are designed to run in CI environments:

```yaml
# Example GitHub Actions workflow
- name: Build WASM
  run: bun run wasm:build

- name: Run unit tests
  run: bun run test:unit

- name: Start server
  run: ./build/bin/ascii-chat server --port 27224 &

- name: Run E2E tests
  run: bun run test:e2e
```

## Troubleshooting

### "WASM module not found"

```bash
# Rebuild WASM modules
bun run wasm:build
```

### "Connection refused" in E2E tests

```bash
# Ensure server is running
./build/bin/ascii-chat server --port 27224

# Check server is listening
lsof -i :27224
```

### "Handshake timeout"

```bash
# Check server logs
./build/bin/ascii-chat --log-level debug server --grep "handshake"

# Check client logs in browser DevTools console
```

### Tests fail with memory errors

```bash
# Clean and rebuild WASM
bun run wasm:clean
bun run wasm:build
```

## Performance

Typical test execution times:

- Unit tests: ~2-5 seconds
- E2E tests: ~10-30 seconds (depends on server startup)

WASM module sizes:

- `client.wasm`: ~1.3MB (includes libsodium, Opus, zstd)
- `mirror.wasm`: ~961KB (minimal, no crypto/audio)

## Additional Resources

- [Vitest Documentation](https://vitest.dev/)
- [Playwright Documentation](https://playwright.dev/)
- [WebAssembly Testing Guide](https://developer.mozilla.org/en-US/docs/WebAssembly/Testing)
- [ascii-chat Crypto Protocol](../../../docs/crypto.md)
