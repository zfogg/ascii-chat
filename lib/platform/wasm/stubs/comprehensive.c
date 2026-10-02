/**
 * @file platform/wasm/stubs/comprehensive.c
 * @brief Comprehensive stubs for WASM build - network, encoding, analysis
 * @ingroup platform
 */

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/util/fps.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/network/mdns/discovery.h>
#include <ascii-chat/util/lifecycle.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declarations for opaque types */
typedef void h265_encoder_t;
typedef void video_frame_t;
typedef void acds_client_config_t;
typedef void acds_client_t;
typedef void audio_context_t;
typedef void client_audio_pipeline_config_t;
typedef void acip_client_callbacks_t;
typedef void wav_writer_t;
typedef void acds_session_lookup_result_t;

/* ===== Socket stubs ===== */

bool socket_is_valid(socket_t sock) {
  return sock >= 0;
}

int socket_close(socket_t sock) {
  (void)sock;
  return 0;
}

int socket_shutdown(socket_t sock, int how) {
  (void)sock;
  (void)how;
  return 0;
}

ssize_t socket_send(socket_t sock, const void *buf, size_t len, int flags) {
  (void)sock;
  (void)buf;
  (void)len;
  (void)flags;
  return -1;
}

ssize_t socket_recv(socket_t sock, void *buf, size_t len, int flags) {
  (void)sock;
  (void)buf;
  (void)len;
  (void)flags;
  return -1;
}

int socket_poll(struct pollfd *fds, unsigned long nfds, int64_t timeout_ns) {
  (void)fds;
  (void)nfds;
  (void)timeout_ns;
  return 0;
}

bool socket_is_would_block_error(int error_code) {
  (void)error_code;
  return false;
}

bool socket_is_invalid_socket_error(int error_code) {
  (void)error_code;
  return false;
}

const char *socket_get_error_string(void) {
  return "Socket error";
}

int socket_get_last_error(void) {
  return 0;
}

/* ===== H.265 encoding stubs ===== */

asciichat_error_t h265_encode(h265_encoder_t *encoder, uint16_t width, uint16_t height, const uint8_t *pixel_data, uint8_t *out_data, size_t *out_len) {
  (void)encoder;
  (void)width;
  (void)height;
  (void)pixel_data;
  (void)out_data;
  (void)out_len;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "H.265 encoding not supported in WASM");
}

void h265_encoder_destroy(h265_encoder_t *encoder) {
  (void)encoder;
}

asciichat_error_t h265_encoder_flush(h265_encoder_t *encoder, uint8_t *out_data, size_t *out_len) {
  (void)encoder;
  (void)out_data;
  (void)out_len;
  return ASCIICHAT_OK;
}

void h265_encoder_request_keyframe(h265_encoder_t *encoder) {
  (void)encoder;
}

/* ===== Audio analysis stubs ===== */

void audio_analysis_destroy(void) {
}

void audio_analysis_print_report(void) {
}

void audio_analysis_track_received_packet(uint32_t timestamp) {
  (void)timestamp;
}

void audio_analysis_track_sent_packet(uint32_t timestamp) {
  (void)timestamp;
}

void audio_analysis_track_sent_sample(float sample) {
  (void)sample;
}

void audio_analysis_track_received_sample(float sample) {
  (void)sample;
}

/* ===== Audio samples stubs ===== */

typedef struct audio_ring_buffer audio_ring_buffer_t;
typedef struct audio_pipeline audio_pipeline_t;
typedef struct client_audio_pipeline client_audio_pipeline_t;

size_t audio_ring_buffer_available_read(audio_ring_buffer_t *rb) {
  (void)rb;
  return 0;
}

void audio_set_pipeline(audio_context_t *ctx, void *pipeline) {
  (void)ctx;
  (void)pipeline;
}

asciichat_error_t audio_write_samples(void *ctx, const float *samples, int count) {
  (void)ctx;
  (void)samples;
  (void)count;
  return ASCIICHAT_OK;
}

asciichat_error_t audio_read_samples(void *ctx, float *buffer, int count) {
  (void)ctx;
  (void)buffer;
  (void)count;
  return ASCIICHAT_OK;
}

/* ===== Client audio pipeline stubs ===== */

client_audio_pipeline_t *client_audio_pipeline_create(const client_audio_pipeline_config_t *config) {
  (void)config;
  return NULL;
}

void client_audio_pipeline_default_config(void) {
}

void client_audio_pipeline_destroy(client_audio_pipeline_t *pipeline) {
  (void)pipeline;
}

asciichat_error_t client_audio_pipeline_capture(client_audio_pipeline_t *pipeline, const float *samples, size_t count) {
  (void)pipeline;
  (void)samples;
  (void)count;
  return ASCIICHAT_OK;
}

void client_audio_pipeline_playback(client_audio_pipeline_t *pipeline, const float *samples, size_t count) {
  (void)pipeline;
  (void)samples;
  (void)count;
}

/* ===== ACIP protocol stubs ===== */

asciichat_error_t acip_send_image_frame(acip_transport_t *transport, const void *pixel_data, uint32_t width,
                                        uint32_t height, uint32_t pixel_format) {
  (void)transport;
  (void)pixel_data;
  (void)width;
  (void)height;
  (void)pixel_format;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "ACIP not supported in WASM");
}

asciichat_error_t acip_client_receive_and_dispatch(acip_transport_t *transport,
                                                   const acip_client_callbacks_t *callbacks) {
  (void)transport;
  (void)callbacks;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "ACIP not supported in WASM");
}

void acip_transport_destroy(acip_transport_t *transport) {
  (void)transport;
}

acip_transport_t *acip_tcp_transport_create(const char *name, socket_t sockfd, crypto_context_t *crypto_ctx) {
  (void)name;
  (void)sockfd;
  (void)crypto_ctx;
  return NULL;
}

/* ===== Connection stubs ===== */

typedef void tcp_client_t;

asciichat_error_t connection_attempt_tcp(const char *address, int port) {
  (void)address;
  (void)port;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "TCP not supported in WASM");
}



/* ===== WAV file output stubs ===== */

bool wav_dump_enabled(void) {
  return false;
}

void *wav_writer_open(const char *path) {
  (void)path;
  return NULL;
}

int wav_writer_write(wav_writer_t *writer, const float *samples, int num_samples) {
  (void)writer;
  (void)samples;
  (void)num_samples;
  return 0;
}

void wav_writer_close(void *wav) {
  (void)wav;
}

/* ===== WebRTC peer manager stubs ===== */

typedef void webrtc_peer_manager_t;

asciichat_error_t webrtc_peer_manager_handle_sdp(webrtc_peer_manager_t *mgr, const char *sdp) {
  (void)mgr;
  (void)sdp;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "WebRTC not supported in WASM");
}

int webrtc_peer_manager_handle_ice(webrtc_peer_manager_t *mgr, const char *candidate) {
  (void)mgr;
  (void)candidate;
  return -1;
}

/* ===== Lifecycle stubs ===== */

void lifecycle_destroy_commit(lifecycle_t *lc) {
  (void)lc;
}

bool lifecycle_destroy_once(lifecycle_t *lc) {
  (void)lc;
  return true;
}

bool lifecycle_reset(lifecycle_t *lc) {
  (void)lc;
  return true;
}

/* ===== ACDS discovery stubs ===== */

void acds_client_config_init_defaults(acds_client_config_t *config) {
  (void)config;
}

acds_client_t *acds_client_connect(const acds_client_config_t *config, void *ctx) {
  (void)config;
  (void)ctx;
  return NULL;
}

void acds_client_disconnect(acds_client_t *client) {
  (void)client;
}

asciichat_error_t acds_session_lookup(acds_client_t *client, const char *session_string,
                                      acds_session_lookup_result_t *result) {
  (void)client;
  (void)session_string;
  (void)result;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "ACDS not supported in WASM");
}

/* ===== Crypto stubs ===== */

asciichat_error_t parse_private_key(const char *key_data, size_t key_len) {
  (void)key_data;
  (void)key_len;
  return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key parsing not supported in WASM");
}

bool validate_ssh_key_file(const char *path) {
  (void)path;
  return false;
}

asciichat_error_t discovery_keys_verify(const char *acds_server, const char *key_spec,
                                        uint8_t pubkey_out[32]) {
  (void)acds_server;
  (void)key_spec;
  (void)pubkey_out;
  return SET_ERRNO(ERROR_CRYPTO_KEY, "discovery_keys_verify not supported in WASM");
}

void pubkey_to_hex(const uint8_t pubkey[32], char hex_out[65]) {
  (void)pubkey;
  if (hex_out) {
    hex_out[0] = '\0';
  }
}




/* ===== Thread pool stubs ===== */

asciichat_error_t thread_pool_spawn(void *pool, void *(*thread_func)(void *), void *thread_arg, int stop_id, const char *name) {
  (void)pool;
  (void)thread_func;
  (void)thread_arg;
  (void)stop_id;
  (void)name;
  return ASCIICHAT_OK;  // No-op in WASM (no real threads)
}

asciichat_error_t thread_pool_stop_all(void *pool) {
  (void)pool;
  return ASCIICHAT_OK;  // No-op in WASM
}

/* ===== Platform stubs ===== */

bool platform_is_interactive(void) {
  return false;
}
