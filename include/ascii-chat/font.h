/**
 * @file font.h
 * @ingroup font
 * @brief Font resolution and bundled font data
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/platform/font.h>

// Bundled fonts — generated from TTF files at build time.
// Matrix Resurrected font: The default choice for render-file output (cool matrix-style font).
// Selected as the primary choice for --render-file mode when no font is specified.
extern const unsigned char g_font_matrix_resurrected[];
extern const size_t        g_font_matrix_resurrected_size;

// Default monospace font (DejaVu Sans Mono): Reliable fallback for render-file output.
// Used as a last-resort fallback when system fonts are unavailable or matrix font fails.
extern const unsigned char g_font_default[];
extern const size_t        g_font_default_size;
