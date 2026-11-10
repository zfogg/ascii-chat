#pragma once

#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { HTTP_RENDER_SOURCE_UPLOAD, HTTP_RENDER_SOURCE_FILE, HTTP_RENDER_SOURCE_URL,
               HTTP_RENDER_SOURCE_WEBCAM, HTTP_RENDER_SOURCE_VISUALIZATION } http_render_source_type_t;
typedef enum { HTTP_RENDER_OUTPUT_TEXT, HTTP_RENDER_OUTPUT_JSON, HTTP_RENDER_OUTPUT_HTML,
               HTTP_RENDER_OUTPUT_FILE } http_render_output_type_t;

typedef struct {
  http_render_source_type_t type;
  char path[PLATFORM_MAX_PATH_LENGTH];
  char url[OPTIONS_BUFF_SIZE];
  char mime_type[128];
  size_t body_length;
} http_render_source_t;

typedef struct {
  int width, height, color_mode, render_mode, renderer;
  char palette[OPTIONS_BUFF_SIZE];
  char palette_chars[OPTIONS_BUFF_SIZE];
  char render_theme[OPTIONS_BUFF_SIZE];
  int render_font_size;
} http_render_options_t;

typedef struct {
  http_render_source_t source;
  http_render_options_t options;
  http_render_output_type_t output_type;
  uint32_t timeout_ms;
  size_t max_output_bytes;
  bool restricted;
} http_render_request_t;

typedef struct {
  char *ascii;
  size_t ascii_length;
  char output_path[PLATFORM_MAX_PATH_LENGTH];
  char mime_type[128];
  uint64_t elapsed_ms;
  bool timed_out;
  bool cancelled;
} http_render_result_t;

void http_render_request_init(http_render_request_t *request);
void http_render_result_destroy(http_render_result_t *result);

asciichat_error_t http_renderer_render_file(const char *path, const http_render_request_t *request,
                                            http_render_result_t *result);
