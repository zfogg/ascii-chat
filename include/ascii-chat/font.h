/**
 * @file font.h
 * @ingroup font
 * @brief Font resolution and bundled font data
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/platform/font.h>

// Bundled Matrix Resurrected font — generated from TTF at build time.
// Selected as a last-resort fallback when system font resolution fails.
extern const unsigned char g_font_matrix_resurrected[];
extern const size_t        g_font_matrix_resurrected_size;
