# PR 512 rendering evidence

Tested code: `a9ad621303cc8140979b78a4b5ce4ad25c6df22d`.

240 actual `ascii-chat mirror --test-pattern` runs completed with exit 0.
Each backend/environment ran both patterns, foreground/background/half-block,
and mono/16/256/truecolor at 120x40. The 20 PNG matrices each contain 12 terminal
screenshots. Every matrix was visually inspected for the gradient and square,
rainbow and circle, readable `test` label, color mode behavior, half-block output,
and visible corruption. Animated object positions differ between runs.

| Environment | Backends | Cases |
| --- | --- | ---: |
| Docker Linux x86-64 on Ryzen 7 5800X | scalar, SSE2, SSSE3, AVX2 | 96 |
| zachbook-pro: Intel i7-9750H macOS | SSE2, SSSE3, AVX2 | 72 |
| pi5: Cortex-A76 Linux ARM64 | scalar, NEON | 48 |
| pi5: QEMU 11.1.1 `-cpu max`, emulated | SVE, 64-byte vector length | 24 |

The named Mac is Intel and cannot execute NEON. The Pi 5 has NEON but no SVE.
SVE screenshots are explicitly emulated; no native SVE hardware or performance
claim is made. `hosts/` records hardware, compiler versions, the checked-out SHA,
and dispatch probes linked against the same built rendering library.

## Capture and screenshot method

`capture_matrix.py` runs the real application through a Unix PTY (124x44 cells),
sets `ASCII_CHAT_SIMD`, and preserves stdout and stderr separately. It records
the complete command, exit status, binary and shared-library hashes, source SHA,
and byte counts. `extract_frames.py` selects one complete frame near the middle
of each run, preserving its exact bytes; manifests record its byte range and
SHA256 along with the full stream hash. Full original streams remain in the
isolated host test directories. The committed `captures/` files are those exact
frame excerpts plus complete stderr logs.

`screenshots.cjs` replays these ANSI bytes through xterm.js 6.0.0 and takes browser
screenshots with Playwright 1.58.2 Chromium. These are terminal replay screenshots,
not photographs of the remote desktop and not images drawn from the source RGB.
There is no manual recoloring or image reconstruction. Install the screenshot
runtime with `npm install --prefix terminal @xterm/xterm@6.0.0 @playwright/test@1.58.2`.
Pass the capture directory and screenshot output directory to `node screenshots.cjs`.
Each PNG uses columns mono/16/256/truecolor and rows foreground/background/half-block.

`validation.json` and `screenshots/*.cells.json` record the checks: all 240 exits
are zero, frames have 120 columns by 40 rows, no replacement characters occur,
colored background and half-block modes paint background cells, and foreground
modes do not. The complete stderr scan found no ERROR/FATAL, ASan error, or UBSan
runtime error. Debug allocator warnings/reports remain preserved in stderr;
they report 199 bytes retained at `lib/util/pcre2.c`, outside renderer allocation
sites. This report does not claim empty stderr or a complete application leak audit.

## Bugs found and fixed by these runs

- `032964ec9`: capability dispatch discarded background mode for 16/256 colors.
- `a9ad62130`: options parsing unconditionally restored previous video flip values,
  overriding explicit `--flip-x=false` and mirroring the test label on macOS.

Both regression tests passed against the production library under Docker Criterion;
see `hosts/options-test.log` and `hosts/capability-test.log`. Renderer cell tests also
passed on Linux x86-64 with ASan/UBSan and ARM64 QEMU at five vector lengths.
The `ascii-chat` application target built on Docker, macOS, and Pi 5. The Pi's
optional all-target shell completion generation hit its 1.5-second timeout;
building the requested application target succeeded.
