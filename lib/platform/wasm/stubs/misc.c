/**
 * @file platform/wasm/stubs/misc.c
 * @brief Miscellaneous stubs for WASM
 * @ingroup platform
 */

#include <ascii-chat/audio/audio.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>
#include <stddef.h>

/* Forward declarations for opaque types */
typedef struct video_fps_counter_t video_fps_counter_t;

/* Audio stubs */
asciichat_error_t audio_stop_duplex(audio_context_t *ctx) {
  (void)ctx;
  return ASCIICHAT_OK;
}

void audio_terminate_portaudio_final(void) {
}

/* Debug sync stubs */
void debug_sync_cleanup_thread(void) {
}

/* FPS stubs */
void fps_init(video_fps_counter_t *fps, double target_fps) {
  (void)fps;
  (void)target_fps;
}

uint64_t fps_frame_ns(video_fps_counter_t *fps) {
  (void)fps;
  return 41666666;  // ~24 FPS default
}

/* Terminal/rendering stubs */
tty_info_t get_current_tty(void) {
  tty_info_t tty = {0};
  return tty;
}

terminal_capabilities_t apply_color_mode_override(terminal_capabilities_t caps) {
  return caps;
}

typedef void render_file_t;
typedef void media_source_t;

render_file_t *render_file_create(const char *output_file) {
  (void)output_file;
  return NULL;  // No file rendering in browser
}

void render_file_set_audio_source(render_file_t *rf, media_source_t *source) {
  (void)rf;
  (void)source;
}

void render_file_set_video_source(render_file_t *rf, media_source_t *source) {
  (void)rf;
  (void)source;
}

void render_file_write_frame(render_file_t *rf, const uint8_t *data, size_t len) {
  (void)rf;
  (void)data;
  (void)len;
}

void render_file_destroy(render_file_t *rf) {
  (void)rf;
}

void render_file_set_snapshot_actual_duration(render_file_t *rf, uint64_t duration_ns) {
  (void)rf;
  (void)duration_ns;
}

/* Terminal stubs */
asciichat_error_t terminal_clear_scrollback(int fd) {
  (void)fd;
  return ASCIICHAT_OK;
}

asciichat_error_t terminal_reset(int fd) {
  (void)fd;
  return ASCIICHAT_OK;
}

/* Symbol cache stubs */
typedef void symbol_cache_t;
void symbol_cache_destroy(symbol_cache_t *cache) {
  (void)cache;
}

/* Terminal resize detection stubs */
void terminal_stop_resize_detection(void) {
}

/* Known hosts stubs */
void known_hosts_destroy(void) {
}

/* Platform cleanup stubs */
void platform_cleanup_binary_path_cache(void) {
}

asciichat_error_t platform_restore_timer_resolution(void) {
  return ASCIICHAT_OK;
}

/* H.265 encoder stubs */
typedef void h265_encoder_t;
h265_encoder_t *h265_encoder_create(int width, int height, double fps) {
  (void)width;
  (void)height;
  (void)fps;
  return NULL;  // No video encoding in WASM
}

/* Platform signal stubs */
typedef void (*signal_handler_t)(int);

signal_handler_t platform_signal(int sig, signal_handler_t handler) {
  (void)sig;
  (void)handler;
  return NULL;
}

void platform_force_exit(int exit_code) {
  (void)exit_code;
}

void set_interrupt_callback(void (*callback)(void)) {
  (void)callback;
}

void signal_exit(void) {
}

bool should_exit(void) {
  return false;
}

/* mDNS stubs */
typedef void mdns_query_t;
typedef void mdns_result_t;

mdns_query_t *ui_mdns_query(const char *service) {
  (void)service;
  return NULL;
}

mdns_result_t *ui_mdns_get_best_address(mdns_query_t *query) {
  (void)query;
  return NULL;
}

mdns_result_t *ui_mdns_select(mdns_result_t *results) {
  (void)results;
  return NULL;
}

void ui_mdns_free_results(mdns_result_t *results) {
  (void)results;
}
