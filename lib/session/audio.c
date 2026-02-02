/**
 * @file session/audio.c
 * @brief 🔊 Session-level audio coordination implementation
 * @ingroup session
 *
 * Implements the session audio wrapper for coordinating audio capture,
 * playback, and mixing at the session level.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/session/audio.h>
#include <ascii-chat/common.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/asciichat_errno.h>

#include <string.h>

/* ============================================================================
 * Session Audio Context Structure
 * ============================================================================ */

/**
 * @brief Maximum number of audio sources for mixing
 */
#define SESSION_AUDIO_MAX_SOURCES 32

/**
 * @brief Per-source audio buffer info for mixing
 */
typedef struct {
  uint32_t source_id;
  bool active;
  audio_ring_buffer_t *buffer;
} session_audio_source_t;

/**
 * @brief Internal session audio context structure
 *
 * Contains the underlying audio context plus optional mixing state
 * for host mode.
 */
struct session_audio_ctx {
  /** @brief Underlying audio context from lib/audio */
  audio_context_t audio_ctx;

  /** @brief True if this is a host context (has mixing capabilities) */
  bool is_host;

  /** @brief Audio streams are currently running */
  bool running;

  /** @brief Context is fully initialized */
  bool initialized;

  /** @brief Audio sources for mixing (host only) */
  session_audio_source_t sources[SESSION_AUDIO_MAX_SOURCES];

  /** @brief Number of active sources (host only) */
  int source_count;

  /** @brief Temporary buffer for mixing operations */
  float *mix_buffer;

  /** @brief Size of mix buffer in samples */
  size_t mix_buffer_size;
};

/* ============================================================================
 * Session Audio Lifecycle Functions
 * ============================================================================ */

session_audio_ctx_t *session_audio_create(bool is_host) {
  // Allocate context
  session_audio_ctx_t *ctx = SAFE_CALLOC(1, sizeof(session_audio_ctx_t), session_audio_ctx_t *);

  ctx->is_host = is_host;
  ctx->running = false;

  // Initialize underlying audio context
  asciichat_error_t result = audio_init(&ctx->audio_ctx);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to initialize audio context: %d", result);
    SAFE_FREE(ctx);
    return NULL;
  }

  // Initialize mixing resources for host
  if (is_host) {
    // Allocate mix buffer (enough for one audio buffer worth)
    ctx->mix_buffer_size = AUDIO_BUFFER_SIZE * 4; // Extra space for safety
    ctx->mix_buffer = SAFE_CALLOC(ctx->mix_buffer_size, sizeof(float), float *);

    // Initialize source array
    for (int i = 0; i < SESSION_AUDIO_MAX_SOURCES; i++) {
      ctx->sources[i].source_id = 0;
      ctx->sources[i].active = false;
      ctx->sources[i].buffer = NULL;
    }
    ctx->source_count = 0;
  }

  ctx->initialized = true;
  return ctx;
}

void session_audio_destroy(session_audio_ctx_t *ctx) {
  if (!ctx) {
    return;
  }

  // Stop audio if running
  if (ctx->running) {
    session_audio_stop(ctx);
  }

  // Cleanup host-specific resources
  if (ctx->is_host) {
    // Destroy source buffers
    for (int i = 0; i < SESSION_AUDIO_MAX_SOURCES; i++) {
      if (ctx->sources[i].buffer) {
        audio_ring_buffer_destroy(ctx->sources[i].buffer);
        ctx->sources[i].buffer = NULL;
      }
    }

    // Free mix buffer
    if (ctx->mix_buffer) {
      SAFE_FREE(ctx->mix_buffer);
    }
  }

  // Destroy underlying audio context
  audio_destroy(&ctx->audio_ctx);

  ctx->initialized = false;
  SAFE_FREE(ctx);
}

/* ============================================================================
 * Session Audio Control Functions
 * ============================================================================ */

asciichat_error_t session_audio_start_capture(session_audio_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_audio_start_capture: invalid context");
  }

  // Use full duplex for proper echo cancellation
  return session_audio_start_duplex(ctx);
}

asciichat_error_t session_audio_start_playback(session_audio_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_audio_start_playback: invalid context");
  }

  // Use full duplex for proper echo cancellation
  return session_audio_start_duplex(ctx);
}

asciichat_error_t session_audio_start_duplex(session_audio_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_audio_start_duplex: invalid context");
  }

  if (ctx->running) {
    return ASCIICHAT_OK; // Already running
  }

  asciichat_error_t result = audio_start_duplex(&ctx->audio_ctx);
  if (result == ASCIICHAT_OK) {
    ctx->running = true;
  }

  return result;
}

void session_audio_stop(session_audio_ctx_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->running) {
    return;
  }

  (void)audio_stop_duplex(&ctx->audio_ctx);
  ctx->running = false;
}

bool session_audio_is_running(session_audio_ctx_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return false;
  }
  return ctx->running;
}

/* ============================================================================
 * Session Audio I/O Functions
 * ============================================================================ */

size_t session_audio_read_captured(session_audio_ctx_t *ctx, float *buffer, size_t num_samples) {
  if (!ctx || !ctx->initialized || !buffer || num_samples == 0) {
    return 0;
  }

  asciichat_error_t result = audio_read_samples(&ctx->audio_ctx, buffer, (int)num_samples);
  if (result != ASCIICHAT_OK) {
    return 0;
  }

  return num_samples;
}

asciichat_error_t session_audio_write_playback(session_audio_ctx_t *ctx, const float *buffer, size_t num_samples) {
  if (!ctx || !ctx->initialized || !buffer) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_audio_write_playback: invalid parameter");
  }

  return audio_write_samples(&ctx->audio_ctx, buffer, (int)num_samples);
}

/* ============================================================================
 * Session Audio Host-Only Functions (Mixing)
 * ============================================================================ */

asciichat_error_t session_audio_add_source(session_audio_ctx_t *ctx, uint32_t source_id) {
  if (!ctx || !ctx->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_audio_add_source: invalid context");
  }

  if (!ctx->is_host) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_audio_add_source: not a host context");
  }

  // Check if source already exists
  for (int i = 0; i < SESSION_AUDIO_MAX_SOURCES; i++) {
    if (ctx->sources[i].active && ctx->sources[i].source_id == source_id) {
      return ASCIICHAT_OK; // Already registered
    }
  }

  // Find empty slot
  for (int i = 0; i < SESSION_AUDIO_MAX_SOURCES; i++) {
    if (!ctx->sources[i].active) {
      ctx->sources[i].source_id = source_id;
      ctx->sources[i].active = true;
      ctx->sources[i].buffer = audio_ring_buffer_create();
      if (!ctx->sources[i].buffer) {
        ctx->sources[i].active = false;
        return SET_ERRNO(ERROR_MEMORY, "Failed to create audio buffer for source");
      }
      ctx->source_count++;
      return ASCIICHAT_OK;
    }
  }

  return SET_ERRNO(ERROR_RESOURCE_EXHAUSTED, "Maximum audio sources reached");
}

void session_audio_remove_source(session_audio_ctx_t *ctx, uint32_t source_id) {
  if (!ctx || !ctx->initialized || !ctx->is_host) {
    return;
  }

  for (int i = 0; i < SESSION_AUDIO_MAX_SOURCES; i++) {
    if (ctx->sources[i].active && ctx->sources[i].source_id == source_id) {
      if (ctx->sources[i].buffer) {
        audio_ring_buffer_destroy(ctx->sources[i].buffer);
        ctx->sources[i].buffer = NULL;
      }
      ctx->sources[i].active = false;
      ctx->sources[i].source_id = 0;
      ctx->source_count--;
      return;
    }
  }
}

asciichat_error_t session_audio_write_source(session_audio_ctx_t *ctx, uint32_t source_id, const float *samples,
                                             size_t num_samples) {
  if (!ctx || !ctx->initialized || !samples) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_audio_write_source: invalid parameter");
  }

  if (!ctx->is_host) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_audio_write_source: not a host context");
  }

  // Find the source
  for (int i = 0; i < SESSION_AUDIO_MAX_SOURCES; i++) {
    if (ctx->sources[i].active && ctx->sources[i].source_id == source_id) {
      if (ctx->sources[i].buffer) {
        return audio_ring_buffer_write(ctx->sources[i].buffer, samples, (int)num_samples);
      }
      return SET_ERRNO(ERROR_INVALID_STATE, "Source buffer not initialized");
    }
  }

  return SET_ERRNO(ERROR_NOT_FOUND, "Audio source not found: %u", source_id);
}

size_t session_audio_mix_excluding(session_audio_ctx_t *ctx, uint32_t exclude_id, float *output, size_t num_samples) {
  if (!ctx || !ctx->initialized || !output || num_samples == 0) {
    return 0;
  }

  if (!ctx->is_host) {
    return 0;
  }

  // Initialize output to silence
  memset(output, 0, num_samples * sizeof(float));

  // Ensure we have a mix buffer
  if (!ctx->mix_buffer || num_samples > ctx->mix_buffer_size) {
    return 0;
  }

  int sources_mixed = 0;

  // Mix all active sources except the excluded one
  for (int i = 0; i < SESSION_AUDIO_MAX_SOURCES; i++) {
    if (!ctx->sources[i].active || ctx->sources[i].source_id == exclude_id) {
      continue;
    }

    if (!ctx->sources[i].buffer) {
      continue;
    }

    // Read samples from this source
    size_t samples_read = audio_ring_buffer_read(ctx->sources[i].buffer, ctx->mix_buffer, num_samples);

    if (samples_read == 0) {
      continue;
    }

    // Add to output mix
    for (size_t j = 0; j < samples_read; j++) {
      output[j] += ctx->mix_buffer[j];
    }

    sources_mixed++;
  }

  // Apply simple clipping prevention if multiple sources were mixed
  if (sources_mixed > 1) {
    float scale = 1.0f / (float)sources_mixed;
    for (size_t i = 0; i < num_samples; i++) {
      output[i] *= scale;
      // Soft clipping
      if (output[i] > 1.0f) {
        output[i] = 1.0f;
      } else if (output[i] < -1.0f) {
        output[i] = -1.0f;
      }
    }
  }

  return num_samples;
}
