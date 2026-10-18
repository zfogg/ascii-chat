# ACIP packet preparation validation

The shared planner borrows the caller's vectors. Plaintext TCP keeps vectored delivery;
no payload is copied or allocated by preparation. Ciphertext is written into storage
owned by each transport, which retains its existing delivery/queue lifetime. WebRTC
also uses the shared encrypted envelope after its per-peer negotiation.

## Reproducing the measurements

Build `bench-acip-prepare` and run `build/bin/bench-acip-prepare`. The fixture compares
reproductions of the previous preparation algorithms with the shared implementation,
using actual encryption on 64 KiB packets, 2,000 iterations per sample, three samples,
and alternating before/after order. Allocation and explicit copy counts cover
preparation only, excluding encryption internals and transport queue copies. Reported
throughput is median preparation throughput, not network or presentation throughput.

Windows x64, Clang, CMake Dev build, October 18, 2026:

| Path | Allocations before/after | Explicit copy bytes before/after | MiB/s before/after |
| --- | --- | --- | --- |
| Sync TCP contiguous | 1 / 1 | 22 / 44 | 637.04 / 617.27 |
| Sync TCP vectors | 2 / 2 | 65,558 / 65,580 | 619.14 / 616.54 |
| Async TCP contiguous | 2 / 1 | 65,536 / 22 | 632.68 / 631.33 |
| Async TCP vectors | 2 / 2 | 65,536 / 65,558 | 568.24 / 619.89 |
| WebSocket contiguous | 2 / 1 | 65,598 / 44 | 621.12 / 598.70 |

The additional 22-byte header read centralizes policy inspection. Plaintext paths
retain zero allocations and zero payload copies; the planner reads the header.
Timing varies across samples, so these measurements do not establish a network
throughput improvement. They demonstrate preserved vector behavior and reduced
allocation/copy work on contiguous encrypted async TCP and WebSocket paths.

## Coverage

- `test-acip-prepare`: borrowed plaintext vectors, handshake exceptions, identical
  contiguous/vector ciphertext, decryption, bounds, allocation/crypto failures,
  synchronous and asynchronous TCP, and continued delivery after local send rejection.
- `python tests/integration/acip_prepare_websocket.py build/bin/test-acip-prepare`:
  real WebSocket binary-frame round trip using the shared envelope.
- `test-webrtc-security`: real libdatachannel peers, 64 KiB round trips, nonce
  advancement, default/encrypted/password/identity/auth-only modes, and rejected
  credentials before application admission.
- `python tests/integration/crypto_policy.py build/bin/ascii-chat`: 15 real CLI
  TCP/WS/WSS policy cases with rendered frames and rejected credentials.
- `python tests/integration/webrtc_security.py build/bin/ascii-chat`: local ACDS,
  CLI host and participant, negotiation through the production peer manager, and
  received/rendered video in six default/password/mutual-key security modes.
  Observed receive/render rates were 57.9-58.0 fps with a 40x12 terminal.
- From `web/web`, `vp exec playwright test --config tests/e2e/peer-security.config.ts`:
  real browser DataChannels and compiled C/WASM peer crypto, chunked round trips,
  policy parity, wrong credentials, and plaintext rejection on encrypted sessions.

WebRTC's native DTLS remains enabled in every mode. ACIP encryption is additional:
secure transports default to DTLS/TLS alone, `--encrypt` enables ACIP encryption,
passwords enable it by default, and `--no-encrypt` allows authentication without
ACIP payload encryption. Media credentials belong to each peer, separately from
ACDS signaling credentials. Existing peers without ACIP negotiation must upgrade
both endpoints for this WebRTC protocol change.
