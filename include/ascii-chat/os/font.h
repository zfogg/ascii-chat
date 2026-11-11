/**
 * @file platform/font.h
 * @ingroup platform
 * @brief Platform-specific font resolution for render-file output
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>
#include <libplatform/font.h>

// Resolve a font spec to a usable form for the current platform.
// spec:        NULL/empty → platform default; bundled name; family name; or absolute path.
// out:         resolved file path or unresolved specification. macOS passes unknown
//              names through; the FreeType renderer does not resolve family names.
// out_is_path: true if out is an absolute file path; false for names or bundled data.
// out_font_data: non-NULL only for bundled fonts; points to TTF data in memory.
// out_font_data_size: size of font_data, or 0 if not used.
