# Logging audit (#139)

Reviewed first-party logging across `lib/`, `src/`, public logging macros, and
browser TypeScript sources. Searches covered severity, hot-loop calls, disabled
logs, sensitive values, rate-limit units, and connection/authentication outcomes.
Vendored libraries, generated WASM binaries, and intentionally verbose test and
instrumentation output are outside the production call-site changes.

| Area | Findings and disposition |
| --- | --- |
| Server/client packet and render paths | Packet receipt, H.265 sends, frame hashes, buffer validation, and render timing were INFO or unsampled DEBUG. Sample repeated diagnostics; retain joins, leaves, stream changes, and failures. Correct string client IDs printed with integer formats. |
| Audio | Normal callback startup/read activity was WARN and worker statistics were INFO. Use DEBUG, sample recurring codec failures, and preserve device selection, fallback, and initialization outcomes. |
| Media/session rendering | Frame queue operations and FFmpeg sample diagnostics flooded INFO. Sample at DEBUG, bound drop warnings, and report failed ASCII conversion instead of silently skipping a frame. Correct recording-warning intervals from nanoseconds to microseconds. |
| Crypto/authentication | Decryption failure serialized the full symmetric key; OpenPGP parsing printed private bytes; private-key format detection printed file contents. Remove all three disclosures. Replace per-packet nonce formatting with sampled size/counter metadata. Preserve handshake, authentication, and rekey events without sampling. |
| TCP/WebSocket/WebRTC/ACDS | Preserve lifecycle and transport errors. Move service-loop, callback, signaling relay, and packet-dispatch traces to sampled DEBUG. Keep TCP connection success visible at INFO, matching WebSocket. Correct WebRTC diagnostic interval units. |
| Discovery/database/NAT | Keep session joins/leaves, host updates, NAT selection, and fallback events. Demote database join-policy implementation details and discovery initialization checkpoints. |
| Options/platform/video/utilities | Demote option parsing, DNS and POSIX keyboard setup checkpoints. Correct Linux/WASM webcam warning cadence. Keep validation errors, device failures, and platform fallback warnings. Existing utility errors and explicit debug/tooling output remain. |
| Browser | Move commit SHA, renderer allocation/resize, and sampled mirror-frame diagnostics to `console.debug`. Preserve browser warnings and errors. |

## Removed and missing diagnostics

- Restore a bounded mixer sample-count diagnostic, fixing its string client-ID
  format. Restore known-hosts file-open tracing; keep `SET_ERRNO_SYS` for the
  failure so logging cannot overwrite its system error first.
- Keep the disabled WebSocket service-thread exit log disabled: its comment
  documents an AddressSanitizer use-after-free during teardown. Restoring it
  requires a separate logger-lifetime fix.
- Keep redundant memory/atomic-registration, aspect-ratio and palette-cache
  traces disabled. They add startup or frame noise without a missing outcome.
- Reviewed removal commit `1abeab007`: session setup failures still propagate
  `SET_ERRNO` to the owning caller. Do not restore the duplicate FATAL messages.
  Existing known-host persistence notices and terminal logging visibility remain.
- Report a NULL ASCII conversion in the session pipeline at bounded ERROR level;
  the loop previously discarded the frame without a diagnostic at that boundary.

## Verification

Build with the default Clang/Ninja preset, then:

```sh
cmake --build build --target ascii-chat test-crypto-logging
python tests/standalone/logging/run.py build/bin/test-crypto-logging
```

Use `.exe` on Windows. The standalone test uses the production crypto and logger,
performs 1,000 encrypted round trips and 1,001 invalid-MAC attempts, checks bounded
output and resumed reporting after the rate-limit interval, and rejects known
key bytes or plaintext in the log. It does not require Criterion.

For browser source changes, run `vp check` in `web/` and
`vp test --config vitest.config.ts` in `web/web/`. Running Vitest at the monorepo
root incorrectly collects Playwright suites and omits the browser test setup.

This audit establishes severity and sampling rules, not a guarantee that every
possible runtime failure has been exercised. Linux/macOS webcam and keyboard
paths need their platform CI; generated WASM artifacts need rebuilding for a
release. The first-party C logger's rate limit is per call site, not per client.
