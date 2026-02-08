/**
 * @file platform/wasm/stubs/audio.h
 * @brief Stub audio types for WASM builds (uses Web Audio API instead of PortAudio)
 * @ingroup platform
 *
 * This header provides minimal audio type definitions for WASM builds.
 * In WASM, audio is handled by Web Audio API via AudioPipeline.ts instead of PortAudio.
 * This stub provides just enough definitions for network packet parsing to compile.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../../asciichat_errno.h"

// Audio batch info structure (needed by packet_parsing.h)
typedef struct {
  uint32_t batch_count;   // Number of audio frames in this batch
  uint32_t total_samples; // Total number of samples across all frames
  uint32_t sample_rate;   // Sample rate in Hz (e.g., 48000)
  uint32_t channels;      // Number of channels (1=mono, 2=stereo)
} audio_batch_info_t;

// Stub audio frame structure for WASM
// In WASM builds, audio is handled by Web Audio API via JavaScript
typedef struct {
  uint8_t *data;
  size_t size;
  uint32_t sample_rate;
  uint8_t channels;
} audio_frame_t;

// Function declarations (stubs - not actually used in client WASM)
asciichat_error_t audio_parse_batch_header(const void *data, size_t len, audio_batch_info_t *out_batch);
asciichat_error_t audio_validate_batch_params(const audio_batch_info_t *batch);
bool audio_is_supported_sample_rate(uint32_t sample_rate);

// No PortAudio types needed in WASM
// Audio capture/playback is handled by AudioPipeline.ts via Web Audio API
