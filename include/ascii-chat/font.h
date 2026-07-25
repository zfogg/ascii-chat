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
// Matrix Resurrected font: selected explicitly with --render-font matrix or automatically by --matrix.
extern const unsigned char g_font_matrix_resurrected[];
extern const size_t        g_font_matrix_resurrected_size;

// Default monospace font (DejaVu Sans Mono): used when no render font is specified.
extern const unsigned char g_font_default[];
extern const size_t        g_font_default_size;
