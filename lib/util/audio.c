/**
 * @file util/audio.c
 * @ingroup util
 * @brief 🔊 Audio packet parsing implementation
 */

#include "audio.h"
#include "endian.h" // For HOST_TO_NET_U16, NET_TO_HOST_U32 (cross-platform)
#include <string.h>
#include "../network/packet.h" // For audio_batch_packet_t
#include "../video/av.h"       // For audio packet structures
#include "log/logging.h"
#include "../core/common.h"

asciichat_error_t audio_parse_batch_header(const void *data, size_t len, audio_batch_info_t *info) {
  if (!data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch header data pointer is NULL");
  }

  if (!info) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch info output pointer is NULL");
  }

  if (len < sizeof(audio_batch_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch header too small (len=%zu, expected=%zu)", len,
                     sizeof(audio_batch_packet_t));
  }

  const audio_batch_packet_t *batch_header = (const audio_batch_packet_t *)data;

  // Unpack network byte order values to host byte order
  info->batch_count = ntohl(batch_header->batch_count);
  info->total_samples = ntohl(batch_header->total_samples);
  info->sample_rate = ntohl(batch_header->sample_rate);
  info->channels = ntohl(batch_header->channels);

  return ASCIICHAT_OK;
}

asciichat_error_t audio_validate_batch_params(const audio_batch_info_t *batch) {
  if (!batch) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch info pointer is NULL");
  }

  // Validate batch_count
  if (batch->batch_count == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch count cannot be zero");
  }

  // Check for reasonable max (256 frames per batch is very generous)
  if (batch->batch_count > 256) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch count too large (batch_count=%u, max=256)", batch->batch_count);
  }

  // Validate channels (1=mono, 2=stereo, max 8 for multi-channel)
  if (batch->channels == 0 || batch->channels > 8) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid channel count (channels=%u, valid=1-8)", batch->channels);
  }

  // Validate sample rate
  if (!audio_is_supported_sample_rate(batch->sample_rate)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported sample rate (sample_rate=%u)", batch->sample_rate);
  }

  // Check for reasonable sample counts
  if (batch->total_samples == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch has zero samples");
  }

  // Each batch typically has samples_per_frame worth of samples
  // For 48kHz at 20ms per frame: 48000 * 0.02 = 960 samples per frame
  // With max 256 frames, that's up to ~245k samples per batch
  if (batch->total_samples > 1000000) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch sample count suspiciously large (total_samples=%u)",
                     batch->total_samples);
  }

  return ASCIICHAT_OK;
}

bool audio_is_supported_sample_rate(uint32_t sample_rate) {
  // List of commonly supported audio sample rates
  static const uint32_t supported_rates[] = {
      8000,   // Telephone quality
      16000,  // Wideband telephony
      24000,  // High quality speech
      32000,  // Good for video
      44100,  // CD quality (less common in VoIP)
      48000,  // Standard professional
      96000,  // High-end professional
      192000, // Ultra-high-end mastering
  };

  const size_t rate_count = sizeof(supported_rates) / sizeof(supported_rates[0]);
  for (size_t i = 0; i < rate_count; i++) {
    if (sample_rate == supported_rates[i]) {
      return true;
    }
  }

  return false;
}
