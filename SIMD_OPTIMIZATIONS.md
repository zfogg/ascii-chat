SIMD optimizations
==================

This document describes how to optimize the code for SIMD (Single Instruction Multiple Data).

We're supporting:
* NEON
* SVE
* AVX2
* SSSE3
* SSE2

Each architecture implemention needs:
* SIMD luminance culculation
* SIMD vector LUT for getting glyphs with TBL instructions
* Scalar tail-end remaining pixels
* Scalar CSI REP run-length encoding
* CSI REP run-length encoding
* Monochrome and 16color and 256color and truecolor modes
* Background and foreground modes
* SIMD color quantization for 16color and 256color modes


1. Data Representation
  • Use interleaved RGB888 or a format you can load efficiently with SIMD (vld3q_u8 for RGB).
  • Define a fast output buffer (outbuf_t) with ob_* helpers to avoid printf.
  • Preallocate generously to minimize reallocs (cap ~ width*height*8).

2. Luminance Calculation (SIMD)
  • Implement luma using fixed-point Rec.601 weights: Y = (77*R + 150*G + 29*B + 128) >> 8.
  • Use NEON vmull_u8/vmlal_u8 (or equivilent) to vectorize across 16 (or maybe more) pixels.
  • Shift down to 6 bits (Y >> 2) to map into a 64-entry glyph LUT.

3. Glyph Mapping
  • Precompute a 64-entry glyph ramp from a base string of the palette (like " .:-=+*#%@").
  • Store ramp in a SIMD instrinsic table (vqtbl4q_u8) for wide vector lookups.
  • For scalar fallback/tails, just index into ramp64.

4. Color Handling
  • Output ANSI truecolor SGR sequences: ESC[38;2;R;G;Bm.
  • Track current (R,G,B) state to avoid redundant color resets.
  • At end of each line, emit ESC[0m\n to clean up state.

5. Run-Length Compression
  • Scan runs of identical (glyph,color) pairs.
  • Print first char, then:
  • If run length large enough, use CSI REP (ESC[n b]).
  • Otherwise just repeat the char literally.
  • Only use REP if it saves bytes: (run-1) > digits(run-1)+3.

6. Row/Block Processing
  • Process 16 pixels at a time with NEON or more with AVX2. We support several SIMD architectures.
  • Scalar loop for remaining tail (<16 pixels remaining). <!-- > -->
  • In the future: process 2 rows at once with upper/lower block glyphs (▀/▄) if vertical resolution tradeoff is okay.

7. Performance Tricks
  • Cache glyph ramp as 64 bytes → SIMD LUT.
  • Use branch-free number formatting (ob_u8, ob_u32) instead of sprintf.
  • Only drop to scalar when absolutely necessary (tail pixels, run compression).
  • Grow output buffer geometrically (*3/2) to avoid realloc churn.
  • Favor sequential memory access (row stride contiguous).

8. Correctness & Portability
  • Always NUL-terminate buffer (ob_term) so it’s a valid C string.
  • Fall back to scalar path when NEON not available (e.g., non-AArch64).
  • Reset colors at the end to avoid “color bleed” in terminals.
  • Test on terminals that support CSI REP; provide option to disable.

9. Optional Enhancements
  • Add different glyph ramps (dense, sparse, Unicode art).
  • Support dithering (Bayer/FS) before luma to improve tone.
  • Quantize colors (like 256-color palette) for higher REP compressibility.
  • Add double-height mode using block characters (▀, ▄) for higher vertical resolution.
  • Multithread rows if the image is large enough to benefit.
