/**
 * @file platform/wasm/stubs/misc.c
 * @brief Miscellaneous stubs for WASM
 * @ingroup platform
 */

#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
typedef void audio_context_t;
typedef void ui_mdns_server_t;

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

render_file_t *render_file_create(const char *output_file, int width, int height, int fps, int vcodec_id, int acodec_id) {
  (void)output_file;
  (void)width;
  (void)height;
  (void)fps;
  (void)vcodec_id;
  (void)acodec_id;
  return NULL;  // No file rendering in browser
}

void render_file_set_audio_source(render_file_t *rf, media_source_t *source, int sample_rate) {
  (void)rf;
  (void)source;
  (void)sample_rate;
}

void render_file_set_video_source(render_file_t *rf, media_source_t *source) {
  (void)rf;
  (void)source;
}

int render_file_write_frame(render_file_t *rf, const uint8_t *data, uint64_t duration_ns) {
  (void)rf;
  (void)data;
  (void)duration_ns;
  return 0;
}

int render_file_destroy(render_file_t *rf) {
  (void)rf;
  return 0;
}

void render_file_set_snapshot_actual_duration(render_file_t *rf, double duration_sec) {
  (void)rf;
  (void)duration_sec;
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
void symbol_cache_destroy(void) {
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

int ui_mdns_select(const ui_mdns_server_t *servers, int count) {
  (void)servers;
  (void)count;
  return -1;
}

void ui_mdns_free_results(mdns_result_t *results) {
  (void)results;
}
