#pragma once

#include <ascii-chat/common.h>
#include <libplatform/atomic.h>
#include <ascii-chat/options/options.h>
#include <libplatform/filesystem.h>
#include <libplatform/thread.h>
#include <libplatform/mutex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTTP_RENDERER_MAX_OUTPUT (8u * 1024u * 1024u)

typedef enum { HTTP_RENDER_SOURCE_UPLOAD, HTTP_RENDER_SOURCE_FILE, HTTP_RENDER_SOURCE_URL, HTTP_RENDER_SOURCE_WEBCAM, HTTP_RENDER_SOURCE_VISUALIZATION } http_render_source_type_t;
typedef enum { HTTP_RENDER_OUTPUT_TEXT, HTTP_RENDER_OUTPUT_JSON, HTTP_RENDER_OUTPUT_HTML, HTTP_RENDER_OUTPUT_FILE, HTTP_RENDER_OUTPUT_FILES } http_render_output_type_t;

#define HTTP_RENDERER_MAX_FRAMES 120

typedef struct {
  char *text;
  size_t text_length;
  char path[PLATFORM_MAX_PATH_LENGTH];
  uint64_t timestamp_ms;
} http_render_frame_t;

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
  void *user_data;
} http_render_request_t;

typedef struct {
  char *ascii;
  size_t ascii_length;
  http_render_frame_t *frames;
  size_t frame_count;
  size_t frame_capacity;
  char output_path[PLATFORM_MAX_PATH_LENGTH];
  char mime_type[128];
  uint64_t elapsed_ms;
  bool timed_out;
  bool cancelled;
  asciichat_error_t error;
} http_render_result_t;

typedef struct http_renderer http_renderer_t;
typedef struct http_renderer_job http_renderer_job_t;

typedef struct {
  unsigned max_jobs;
  size_t max_input_bytes;
  size_t max_output_bytes;
  uint32_t timeout_ms;
  bool restricted;
  const char *renderer_executable;
  void (*on_complete)(http_renderer_job_t *job, const http_render_result_t *result, void *user_data);
  void (*on_error)(http_renderer_job_t *job, asciichat_error_t error, void *user_data);
  void *user_data;
} http_renderer_config_t;

typedef struct {
  char **argv;
  size_t argc;
  char source_path[PLATFORM_MAX_PATH_LENGTH];
  char source_url[OPTIONS_BUFF_SIZE];
  char upload_dir[PLATFORM_MAX_PATH_LENGTH];
  char stage_dir[PLATFORM_MAX_PATH_LENGTH];
  char stage_file[PLATFORM_MAX_PATH_LENGTH];
  char output_path[PLATFORM_MAX_PATH_LENGTH];
  unsigned timeout_ms;
  bool capture_webcam;
  http_render_output_type_t output_type;
  http_render_options_t options;
} http_renderer_process_request_t;

typedef struct {
  char *text;
  size_t length;
  http_render_frame_t *frames;
  size_t frame_count;
  char error[2048];
  uint64_t elapsed_ms;
  bool timed_out;
  char output_path[PLATFORM_MAX_PATH_LENGTH];
} http_renderer_process_result_t;

typedef void (*http_renderer_job_fn)(void *context, atomic_t *cancelled);

struct http_renderer_job {
  platform_thread_t thread;
  atomic_t cancelled;
  atomic_t done;
  http_renderer_job_fn execute;
  void *context;
  bool started;
  void *user_data;
  http_renderer_t *renderer;
  http_render_request_t request;
  http_render_result_t result;
  struct http_renderer_job *next;
};

void http_render_request_init(http_render_request_t *request);
void http_render_result_destroy(http_render_result_t *result);
asciichat_error_t http_render_result_add_text_frame(http_render_result_t *result, const char *text, size_t length, uint64_t timestamp_ms);
asciichat_error_t http_render_result_add_file_frame(http_render_result_t *result, const char *path, uint64_t timestamp_ms);
void http_renderer_process(http_renderer_process_request_t *request, atomic_t *cancelled, http_renderer_process_result_t *result);
void http_renderer_process_result_destroy(http_renderer_process_result_t *result);

asciichat_error_t http_renderer_render_file(const char *path, const http_render_request_t *request, http_render_result_t *result, atomic_t *cancelled);

asciichat_error_t http_renderer_job_start(http_renderer_job_t *job, const char *name, http_renderer_job_fn execute, void *context);
void http_renderer_job_cancel(http_renderer_job_t *job);
bool http_renderer_job_is_done(const http_renderer_job_t *job);
void http_renderer_job_join(http_renderer_job_t *job);
void http_renderer_job_reset(http_renderer_job_t *job);

asciichat_error_t http_renderer_init(http_renderer_t **out, const http_renderer_config_t *config);
asciichat_error_t http_renderer_submit(http_renderer_t *renderer, const http_render_request_t *request, void *user_data, http_renderer_job_t **out_job);
asciichat_error_t http_renderer_cancel(http_renderer_job_t *job);
void http_renderer_reap(http_renderer_t *renderer, bool all);
void http_renderer_stop(http_renderer_t *renderer);
void http_renderer_destroy(http_renderer_t **renderer);
void http_renderer_job_wait(http_renderer_job_t *job);
