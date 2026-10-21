#pragma once

#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/acip/settings.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/buffer.h>

/* Callbacks borrow decoded buffers for the duration of the call. Device and UI
 * ownership remain with the mode; one receive worker owns this protocol state. */
typedef struct {
  asciichat_error_t (*on_frame)(const ascii_frame_packet_t *header, const char *text, void *context);
  asciichat_error_t (*on_samples)(const float *samples, size_t count, void *context);
  asciichat_error_t (*decode_opus)(const void *data, size_t length, float *samples, size_t capacity, size_t *decoded,
                                   void *context);
  void (*on_audio_packet)(size_t length, void *context);
  void *context;
  bool terminal_output;                // Drop valid obsolete geometry during terminal/settings resize transitions.
  buffer_workspace_t *frame_workspace; // Optional; receive worker owns it through callback completion.
} session_protocol_media_t;

typedef struct {
  acip_transport_t *transport; // Borrowed until the receive worker joins.
  session_protocol_media_t media;
  acip_client_callbacks_t control;
  asciichat_error_t callback_result;
  uint64_t packets_received;
  uint64_t frames_received;
  uint64_t frames_skipped_viewport;
  uint64_t audio_packets_received;
  uint32_t active_client_count;
  bool server_state_initialized;
  bool layout_changed;
  uint32_t settings_request;
  uint32_t settings_revision;
  uint32_t settings_status;
  acip_render_settings_t effective_render_settings;
} session_protocol_t;

void session_protocol_init(session_protocol_t *protocol, acip_transport_t *transport, session_protocol_media_t media,
                           acip_client_callbacks_t control);
/* Dispatch borrows its payload from the shared connection executor. */
asciichat_error_t session_protocol_dispatch(session_protocol_t *protocol, packet_type_t type, const void *payload,
                                            size_t length);

/* Physical capability advertisement for the initial join. */
void session_protocol_build_capabilities(unsigned short width, unsigned short height,
                                         terminal_capabilities_packet_t *packet);
typedef asciichat_error_t (*session_protocol_send_settings_fn)(const options_t *values, uint32_t request,
                                                               void *context);
/* Call on the connection owner thread. UI publishes automatic dimensions through options.
 * Each connection retains its own last successfully advertised size; failed sends are retried. */
asciichat_error_t session_protocol_update_settings(terminal_size_t *advertised, uint64_t *advertised_revision,
                                                   session_protocol_send_settings_fn send, void *context);
