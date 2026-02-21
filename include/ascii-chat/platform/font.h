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

// Resolve a font spec to a usable form for the current platform.
// spec:        NULL/empty → OS-guaranteed default; family name; or absolute path.
// out:         absolute file path on Linux; family name on macOS (passed to CoreText).
// out_is_path: true if out is an absolute file path, false if it is a family name.
// out_font_data: non-NULL only for bundled fonts; points to TTF data in memory.
// out_font_data_size: size of font_data, or 0 if not used.
asciichat_error_t platform_font_resolve(const char *spec,
                                        char *out, size_t out_size,
                                        bool *out_is_path,
                                        const uint8_t **out_font_data,
                                        size_t *out_font_data_size);
