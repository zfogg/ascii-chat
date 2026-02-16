/**
 * @file options.h
 * @brief WASM option accessor declarations
 * @ingroup web_common
 *
 * Declares setter/getter functions for common web settings.
 * Implementations in options.c are compiled into both mirror.wasm and client.wasm.
 */

#ifndef ASCII_CHAT_WEB_COMMON_OPTIONS_H
#define ASCII_CHAT_WEB_COMMON_OPTIONS_H

#include <emscripten.h>

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

// Horizontal flip setters/getters
int set_flip_x(int enabled);
int get_flip_x(void);

// Target FPS setters/getters
int set_target_fps(int fps);
int get_target_fps(void);

#endif // ASCII_CHAT_WEB_COMMON_OPTIONS_H
