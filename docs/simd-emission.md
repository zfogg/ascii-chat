# Shared ASCII terminal emission

Each ISA owns its image and half-block processing loops in its `color.c` and
`halfblock.c`. Monochrome uses that ISA's same pixel pipeline. Shared output
allocation, terminal state, and ANSI emission live in `lib/video/ascii/render.c`.
The normal ISA entry points do not call a shared scalar renderer.
SSE2, SSSE3, AVX2, NEON, and SVE each supply vector stages for luminance, RGB
packing, palette lookup, exact indexed-color quantization, run comparison, and
half-block classification. Runtime selection keeps the CPU/OS safety checks;
incomplete fixed-width vector tails use the scalar stages. ISA types do
not cross the shared output boundary in `simd/cells.h`.

The original SSE2/SSSE3 two-half arithmetic, AVX2 32-pixel helpers, NEON
16-pixel structure loads and widening arithmetic, and SVE channel/widening
processing are retained in each ISA's `luminance.c` and used by its row loop. The SVE active
count is bounded by halfword lanes before widening. NEON retains vector UTF-8
compaction with `st1`, `st2`, `st3`, and `st4` stores for uniform glyph lengths;
mixed lengths compact the gathered byte streams in order. Duplicate per-ISA
ANSI decimal/escape builders are replaced by the shared encoders.

The architecture `mono.c`, `color.c`, and `halfblock.c` files also instantiate their
vector implementations from `simd/lookup.c`, `simd/quantize.c`, and `simd/pack.c`,
with per-file ISA flags and no unity/PCH mixing. The shared math expresses one
color contract using vector operations; it does not call the scalar renderer
for complete SIMD blocks. The retained NEON table lookup uses `tbl`, NEON
half-block input uses interleaved `ld3`, x86 uses byte shuffles/gathers where
available, and SVE uses predicated scalable loads and comparisons.

SSE2 has no byte shuffle or gather: palette lookup uses parallel comparisons
against ranges of equal UTF-8 keys. SSSE3 uses byte-shuffle tables, AVX2 uses
32-bit gathers, NEON uses four 64-entry byte tables, and SVE uses predicated
gather loads. Color quantization computes exact palette distances in parallel,
including lower-index tie breaking. Floyd-Steinberg error diffusion remains
sequential because each pixel changes subsequent pixels; packing, palette
lookup, and run comparison still use the selected SIMD backend in that mode.

## Cell and row contract

- One source pixel produces one palette glyph. Palette lookup uses rounded
  BT.601 luminance, the 6-bit bucket `luminance >> 2`, and `cache64`. A glyph is
  copied as a complete UTF-8 sequence; repeated glyphs compare their bytes,
  not their luminance bucket or their first byte.
- Monochrome, ANSI 16-color, xterm 256-color, and truecolor share terminal
  state and row semantics. NEON monochrome retains vector UTF-8 compaction;
  other paths use the common cell/run writer. Dithered 16-color quantization still processes every pixel
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
- Every backend has truecolor, 256-color, 16-color, and monochrome half-block
  entry points. The row driver invokes that backend's vector RGB packing,
  indexed-color quantization, transparency/shade classification, and run
  comparison stages. The capability-aware application path selects them at
  runtime for every color mode. Scalar-named half-block functions explicitly
  select the scalar reference, rather than hiding a runtime SIMD selection.
  Odd heights duplicate the final source row. All-black colored padding resets
  colors and emits spaces; monochrome keeps the existing dark-pixel threshold.
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
and background modes, 16-color dithering, every half-block color mode, odd
heights, padded strides, encoder size boundaries, and buffer growth from one
byte. Stage tests independently compare every backend's packing, lookup,
quantization, and run callbacks against scalar results for every tail length
through 257 and a Cartesian set of palette boundaries.

Local validation on 2026-10-18:

- Linux x86-64: dispatch, forced-scalar, invalid-override, and emission tests pass;
  the emission fixture also passes AddressSanitizer and UndefinedBehaviorSanitizer.
- Windows x64: all three standalone dispatch checks pass with Clang; the changed
  shared and x86 renderer sources pass native Clang syntax checking.
- ARM64 under QEMU 6.2: NEON and SVE cell equivalence passes at SVE vector lengths
  16, 32, 64, 128, and 256 bytes; a cortex-a53 run checks NEON without SVE.
- Instruction audits find vector instructions in packing, lookup, quantization,
  run comparison, and half-block classification for all five backends. Repeat
  with `python tests/simd/audit_cells.py build-emission/emission-test avx2`;
  for ARM pass `--objdump aarch64-linux-gnu-objdump` and backend `neon` or `sve`.
  Sample instructions for every stage are recorded in `instructions-x86.txt`
  and `instructions-arm.txt` alongside the CSV reports.
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
vector length, 64 bytes. Emulation and host scheduling make these
single-run timings approximate; they are not stable regression thresholds.
Allocation counts include malloc/calloc/realloc calls after warming caches.
SIMD is not uniformly faster for these small frames: indexed-color paths benefit
from vector quantization, while some small/truecolor paths pay extra preparation
and dispatch costs. The raw results include those slower cases.
The `halfblock-x86.csv` and `halfblock-arm.csv` reports compare all four
half-block modes across every supported backend using the same workload.
Build and run `halfblock-benchmark` to repeat those measurements. The prior
half-block renderer had only a NEON-specific truecolor entry point; the other
ISA/color-mode combinations are new.

The new renderer uses two allocations per frame (output and vector row scratch)
for every backend. The scratch allocation holds packed pixels and prepared
cell columns for the vector stages. The former SIMD implementations sometimes
used one allocation, while others grew their output buffers. The shared path
uses a checked output capacity and one reusable row scratch allocation.

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

The restored ISA-owned pipelines pass the new cell tests for all of those variants. The raw baseline numbers are
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
