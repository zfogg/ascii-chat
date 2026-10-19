# Shared ASCII terminal emission

All scalar and SIMD entry points use `ascii_render()` in
`lib/video/ascii/render.c`. SSE2, SSSE3, AVX2, NEON, and SVE supply
only their RGB24-to-luminance kernel. Runtime selection and scalar tails keep
the existing CPU/OS safety checks. ISA-specific types never cross this boundary.

## Cell and row contract

- One source pixel produces one palette glyph. Palette lookup uses rounded
  BT.601 luminance, the 6-bit bucket `luminance >> 2`, and `cache64`. A glyph is
  copied as a complete UTF-8 sequence; repeated glyphs compare their bytes,
  not their luminance bucket or their first byte.
- Monochrome, ANSI 16-color, xterm 256-color, and truecolor share the same
  cell/run writer. Dithered 16-color quantization still processes every pixel
  in source order, even within a run.
- Foreground mode colors the glyph. Background mode colors the background
  and selects contrasting black or white text. Indexed modes use the shared
  terminal palette quantizers; architecture-specific approximate palettes
  cannot change the displayed colors.
- Runs compare the glyph and both colors. REP is used only when shorter than
  literal UTF-8 output. Runs stop at row boundaries. Colored rows reset SGR
  before a newline, with no trailing newline after the final row. Monochrome
  output emits no SGR. No erase-to-EOL sequence inherits an active background.
- Dynamic buffer growth checks arithmetic and handles even a one-byte initial
  capacity. The shared renderer reserves a checked worst-case capacity including
  UTF-8, both colors, row endings, and the terminator.
- Half-block modes use the same buffer, glyph-run, SGR, and row-ending helpers.
  NEON delegates to the canonical half-block implementation because those cells
  take their two RGB colors directly, without a luminance transformation.
  Odd heights duplicate the final source row. All-black padding resets colors
  and emits spaces. These specialized half-block rules are unchanged.
- Invalid images, empty palettes, and missing kernels return NULL with error
  context. Returned buffers belong to the caller and use `SAFE_FREE`.

`lib/video/ascii/sgr.c` owns truecolor and 256-color sequence encoding.
`append_truecolor_*` and `append_256color_*` remain compatibility entry points
into those encoders. Indexed SGR caches are thread-local, so concurrent first
use does not publish partially initialized sequences.

## Verification

The Criterion fixture `tests/unit/video/ascii_emission_test.c` also runs through
the isolated Linux target. Only process diagnostics and the debug name registry
are replaced in that target; rendering, palette construction, quantization,
allocation, lifecycle, and locks use production sources.

```sh
cmake -S tests/simd -B build-emission -G Ninja -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Release -DTEST_EMISSION=ON
cmake --build build-emission --parallel 4
ctest --test-dir build-emission --output-on-failure
```

For memory safety, configure another build with
`-DCMAKE_C_FLAGS=-fsanitize=address,undefined`. The fixture independently decodes
SGR, REP, UTF-8, and row boundaries and checks the resulting cells. It explicitly
calls every CPU-supported backend, including lower-priority x86 variants. Cases
cover widths 1 through 257 around vector boundaries, three-row images, noisy
colors, long runs, alternating colors, ASCII and Unicode palettes, foreground
and background modes, 16-color dithering, odd-height half-blocks, encoder size
boundaries, and buffer growth from one byte.

Local validation on 2026-10-18:

- Linux x86-64: dispatch, forced-scalar, invalid-override, and emission tests pass;
  the emission fixture also passes AddressSanitizer and UndefinedBehaviorSanitizer.
- Windows x64: all three standalone dispatch checks pass with Clang; the changed
  shared and x86 renderer sources pass native Clang syntax checking.
- ARM64 under QEMU 6.2: NEON and SVE cell equivalence passes at SVE vector lengths
  16, 32, 64, 128, and 256 bytes; a cortex-a53 run checks NEON without SVE.
- Physical ARM hardware performance is not measured here; emulator throughput
  is not a hardware benchmark.

The full application configure/build attempts were stopped while rebuilding unrelated
FFmpeg (Windows) and OpenSSL (Linux) dependencies; no complete application build
or media snapshot run is claimed. The isolated targets compile and link all
production code involved in this refactor.

## Before/after measurements

Raw CSVs are in `tests/simd/results/issue503/`. The baseline is
`c0b985af819da5b8238d16e733629727d68902a5`. Measurements use Clang 14 `-O3`,
161x61 RGB images, 120 frames, ASCII palettes, truecolor and 256-color foreground,
and two patterns (40-pixel runs and changing colors). The host is an AMD Ryzen 7
5800X through WSL2. ARM measurements use QEMU 6.2 `-cpu max` with its default SVE
vector length, 64 bytes. Concurrent dependency builds and emulation make these
single-run timings approximate; they are not stable regression thresholds.
Allocation counts include malloc/calloc/realloc calls after warming caches.

The new renderer uses two allocations per frame (output and luminance scratch)
for every backend. The former SIMD implementations sometimes used one, while
others grew their output buffers. The shared path trades that scratch allocation
for one tested luminance/output boundary and a checked output capacity.

The baseline cannot supply valid performance comparisons for every backend:

- SSE2 and SSSE3 advance 16 source pixels after emitting eight. Their changing-
  color frames contain roughly half the required output. Their reported speed
  therefore measures incomplete frames.
- AVX2 aborts with heap corruption in this workload before producing timing rows.
- NEON's old color implementation does not compile when included: its helper
  declaration disagrees with its call, and the normal build omitted the NEON
  mono/color files. The baseline ARM benchmark disables NEON to measure SVE.
- SVE truecolor reports only 9,881 bytes even for the changing-color image,
  versus 184,057 bytes from the complete shared renderer. These outputs also
  cannot support a valid speed comparison.

The new cell tests pass for all of those variants. The raw baseline numbers are
retained as evidence of the old workloads, not as equivalent-rendering speedups.
Scalar timings are comparable as rendering workloads, while Unicode lookup and
row-reset semantics are now normalized across all paths.

To reproduce a baseline without changing the checkout:

```sh
python tests/simd/export_emission_baseline.py c0b985af819da5b8238d16e733629727d68902a5 /tmp/emission-before
cmake -S tests/simd -B build-emission-before -G Ninja -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Release -DTEST_EMISSION=ON \
  -DEMISSION_BENCHMARK_BASELINE=/tmp/emission-before
cmake --build build-emission-before --target emission-benchmark
ASCII_CHAT_SIMD=scalar build-emission-before/emission-benchmark
ASCII_CHAT_SIMD=scalar build-emission/emission-benchmark
```

The benchmark accepts an optional backend name to isolate a failing baseline.
For ARM cross-compilation, configure with
`-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64
-DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu -DCMAKE_C_FLAGS=--gcc-toolchain=/usr`.
The old NEON benchmark also requires `-DASCIICHAT_CAN_COMPILE_NEON=OFF` because of the baseline compile
failure. Run emulated tests at an explicit SVE length with
`qemu-aarch64 -L /usr/aarch64-linux-gnu -cpu max build-emission-arm/emission-test 16`.
