/**
 * @file metadata.h
 * @brief Extern declarations for shared metadata arrays
 * @ingroup options
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

// Log level metadata
extern const char *g_log_level_values[];
extern const int g_log_level_integers[];
extern const char *g_log_level_descs[];

// Color setting metadata
extern const char *g_color_setting_values[];
extern const int g_color_setting_integers[];
extern const char *g_color_setting_descs[];

// UTF-8 setting metadata
extern const char *g_utf8_setting_values[];
extern const int g_utf8_setting_integers[];
extern const char *g_utf8_setting_descs[];

// Color mode metadata
extern const char *g_color_mode_values[];
extern const int g_color_mode_integers[];
extern const char *g_color_mode_descs[];

// Color filter metadata
extern const char *g_color_filter_values[];
extern const int g_color_filter_integers[];
extern const char *g_color_filter_descs[];

// Palette metadata
extern const char *g_palette_values[];
extern const int g_palette_integers[];
extern const char *g_palette_descs[];

// Render mode metadata
extern const char *g_render_values[];
extern const int g_render_integers[];
extern const char *g_render_descs[];

// Render-file theme metadata (macOS and Linux only)
extern const char *g_render_theme_values[];
extern const int g_render_theme_integers[];
extern const char *g_render_theme_descs[];

// Audio source metadata
extern const char *g_audio_source_values[];
extern const int g_audio_source_integers[];
extern const char *g_audio_source_descs[];

// Example arrays
extern const char *g_compression_examples[];
extern const char *g_fps_examples[];
extern const char *g_width_examples[];
extern const char *g_height_examples[];
extern const char *g_maxclients_examples[];
extern const char *g_reconnect_examples[];
extern const char *g_webcam_examples[];
extern const char *g_mic_examples[];
extern const char *g_speakers_examples[];
extern const char *g_seek_examples[];
