/**
 * @file options.h
 * @brief WASM option accessor declarations
 * @ingroup web_common
 *
 * Declares setter/getter functions for common web settings.
 * Implementations in options.c are compiled into both mirror.wasm and client.wasm.
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>

#ifdef __cplusplus
extern "C" {
#endif

double wasm_options_revision(void);
asciichat_error_t wasm_options_patch(const char *json, double expected_revision);
char *wasm_options_values(void);
void wasm_options_release(char *json);

// Dimension setters/getters
int set_width(int value);
int get_width(void);
int set_height(int value);
int get_height(void);

// Color mode setters/getters
int set_color_mode(int mode);
int get_color_mode(void);

// Color filter setters/getters
int set_color_filter(int filter);
int get_color_filter(void);

// Palette setters/getters
int set_palette(const char *palette_name);
int get_palette(void);

// Palette characters setters/getters
int set_palette_chars(const char *chars);
const char *get_palette_chars(void);

// Matrix rain setters/getters
int set_matrix_rain(int enabled);
int get_matrix_rain(void);

int set_waveform(int enabled);
int get_waveform(void);
int set_fft(int enabled);
int get_fft(void);

// Horizontal flip setters/getters
int set_flip_x(int enabled);
int get_flip_x(void);

// Vertical flip setters/getters
int set_flip_y(int enabled);
int get_flip_y(void);

int set_render_mode(int mode);
int get_render_mode(void);

// Target FPS setters/getters
int set_target_fps(int fps);
int get_target_fps(void);

// Help text API
const char *get_help_text(int mode, const char *option_name);

#ifdef __cplusplus
}
#endif
