/**
 * @file platform/wasm/stubs/audio.c
 * @brief Audio stubs for WASM (audio not supported in browser environment)
 * @ingroup platform
 */

#include <ascii-chat/audio/audio.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>

void audio_ring_buffer_clear(audio_ring_buffer_t *rb) {
  (void)rb;
  // No-op - audio ring buffer not used in WASM
}

asciichat_error_t audio_init(audio_context_t *ctx) {
  (void)ctx;
  return ASCIICHAT_OK; // No-op in WASM
}

bool audio_should_enable_microphone(audio_source_t source, bool has_media_audio) {
  (void)source;
  (void)has_media_audio;
  return false; // No microphone access in WASM mirror mode
}

void audio_destroy(audio_context_t *ctx) {
  (void)ctx;
  // No-op - no audio to destroy
}

asciichat_error_t audio_start_duplex(audio_context_t *ctx) {
  (void)ctx;
  return SET_ERRNO(ERROR_AUDIO, "Audio duplex not supported in WASM");
}

void audio_flush_playback_buffers(audio_context_t *ctx) {
  (void)ctx;
  // No-op - no audio to flush
}

asciichat_error_t audio_analysis_init(void) {
  return ASCIICHAT_OK;
}
