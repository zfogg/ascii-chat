/**
 * @file client/audio_analysis.h
 * @ingroup client_audio
 * @brief Audio Analysis and Debugging Interface
 *
 * Provides audio quality analysis for troubleshooting audio issues.
 * Tracks sent and received audio characteristics for debugging.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Audio analysis statistics for sent or received audio
 */
typedef struct {
  uint64_t total_samples;     // Total samples processed
  float peak_level;           // Maximum sample value
  float rms_level;            // RMS (root mean square) level
  uint64_t clipping_count;    // Samples > 1.0 (clipping)
  uint64_t silent_samples;    // Samples < 0.001 (silence threshold)
  float dc_offset;            // DC bias in signal
  uint32_t packets_count;     // Number of packets
  uint32_t packets_dropped;   // Dropped packets
  int64_t timestamp_start_us; // Start timestamp (microseconds)
  int64_t timestamp_end_us;   // End timestamp (microseconds)
} audio_analysis_stats_t;

/**
 * @brief Initialize audio analysis
 * @return 0 on success, negative on error
 */
int audio_analysis_init(void);

/**
 * @brief Track sent audio sample
 * @param sample Audio sample value
 */
void audio_analysis_track_sent_sample(float sample);

/**
 * @brief Track sent packet
 * @param size Packet size in bytes
 */
void audio_analysis_track_sent_packet(size_t size);

/**
 * @brief Track received audio sample
 * @param sample Audio sample value
 */
void audio_analysis_track_received_sample(float sample);

/**
 * @brief Track received packet
 * @param size Packet size in bytes
 */
void audio_analysis_track_received_packet(size_t size);

/**
 * @brief Get sent audio statistics
 * @return Pointer to analysis stats (do not free)
 */
const audio_analysis_stats_t *audio_analysis_get_sent_stats(void);

/**
 * @brief Get received audio statistics
 * @return Pointer to analysis stats (do not free)
 */
const audio_analysis_stats_t *audio_analysis_get_received_stats(void);

/**
 * @brief Print audio analysis report
 */
void audio_analysis_print_report(void);

/**
 * @brief Cleanup audio analysis
 */
void audio_analysis_cleanup(void);
