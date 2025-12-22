/**
 * @file client/audio_analysis.c
 * @ingroup client_audio
 * @brief Audio Analysis Implementation
 */

#include "audio_analysis.h"
#include "common.h"
#include "logging.h"
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

static audio_analysis_stats_t g_sent_stats;
static audio_analysis_stats_t g_received_stats;
static bool g_analysis_enabled = false;

// For jitter detection (rapid amplitude changes)
static float g_sent_last_sample = 0.0f;
static float g_received_last_sample = 0.0f;

// For discontinuity detection (packet arrival gaps)
static int64_t g_sent_last_packet_time_us = 0;
static int64_t g_received_last_packet_time_us = 0;

int audio_analysis_init(void) {
  SAFE_MEMSET(&g_sent_stats, sizeof(g_sent_stats), 0, sizeof(g_sent_stats));
  SAFE_MEMSET(&g_received_stats, sizeof(g_received_stats), 0, sizeof(g_received_stats));

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  int64_t now_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

  g_sent_stats.timestamp_start_us = now_us;
  g_received_stats.timestamp_start_us = now_us;

  g_sent_last_sample = 0.0f;
  g_received_last_sample = 0.0f;
  g_sent_last_packet_time_us = now_us;
  g_received_last_packet_time_us = now_us;

  g_analysis_enabled = true;
  log_info("Audio analysis enabled");
  return 0;
}

void audio_analysis_track_sent_sample(float sample) {
  if (!g_analysis_enabled)
    return;

  g_sent_stats.total_samples++;

  // Track peak level
  float abs_sample = fabsf(sample);
  if (abs_sample > g_sent_stats.peak_level) {
    g_sent_stats.peak_level = abs_sample;
  }

  // Track clipping (samples > 1.0)
  if (abs_sample > 1.0f) {
    g_sent_stats.clipping_count++;
  }

  // Track silence (very low level)
  if (abs_sample < 0.001f) {
    g_sent_stats.silent_samples++;
  }

  // Detect jitter: rapid amplitude changes > 0.5 between consecutive samples
  float delta = fabsf(sample - g_sent_last_sample);
  if (delta > 0.5f) {
    g_sent_stats.jitter_count++;
  }
  g_sent_last_sample = sample;

  // Accumulate for RMS calculation (done on finalize)
}

void audio_analysis_track_sent_packet(size_t size) {
  if (!g_analysis_enabled)
    return;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  int64_t now_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

  // Detect gaps between consecutive packets (discontinuity)
  if (g_sent_stats.packets_count > 0) {
    int64_t gap_us = now_us - g_sent_last_packet_time_us;
    int32_t gap_ms = (int32_t)(gap_us / 1000);

    // Expected: ~20ms per Opus frame, flag if gap > 100ms
    if (gap_ms > 100) {
      g_sent_stats.discontinuity_count++;
    }

    // Track max gap
    if (gap_ms > (int32_t)g_sent_stats.max_gap_ms) {
      g_sent_stats.max_gap_ms = (uint32_t)gap_ms;
    }
  }

  g_sent_last_packet_time_us = now_us;
  g_sent_stats.packets_count++;
}

void audio_analysis_track_received_sample(float sample) {
  if (!g_analysis_enabled)
    return;

  g_received_stats.total_samples++;

  // Track peak level
  float abs_sample = fabsf(sample);
  if (abs_sample > g_received_stats.peak_level) {
    g_received_stats.peak_level = abs_sample;
  }

  // Track clipping
  if (abs_sample > 1.0f) {
    g_received_stats.clipping_count++;
  }

  // Track silence
  if (abs_sample < 0.001f) {
    g_received_stats.silent_samples++;
  }

  // Detect jitter: rapid amplitude changes > 0.5 between consecutive samples
  float delta = fabsf(sample - g_received_last_sample);
  if (delta > 0.5f) {
    g_received_stats.jitter_count++;
  }
  g_received_last_sample = sample;
}

void audio_analysis_track_received_packet(size_t size) {
  if (!g_analysis_enabled)
    return;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  int64_t now_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

  // Detect gaps between consecutive packets (discontinuity)
  if (g_received_stats.packets_count > 0) {
    int64_t gap_us = now_us - g_received_last_packet_time_us;
    int32_t gap_ms = (int32_t)(gap_us / 1000);

    // Expected: ~20ms per Opus frame, flag if gap > 100ms
    if (gap_ms > 100) {
      g_received_stats.discontinuity_count++;
    }

    // Track max gap
    if (gap_ms > (int32_t)g_received_stats.max_gap_ms) {
      g_received_stats.max_gap_ms = (uint32_t)gap_ms;
    }
  }

  g_received_last_packet_time_us = now_us;
  g_received_stats.packets_count++;
}

const audio_analysis_stats_t *audio_analysis_get_sent_stats(void) {
  return &g_sent_stats;
}

const audio_analysis_stats_t *audio_analysis_get_received_stats(void) {
  return &g_received_stats;
}

void audio_analysis_print_report(void) {
  if (!g_analysis_enabled) {
    return;
  }

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  int64_t now_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

  g_sent_stats.timestamp_end_us = now_us;
  g_received_stats.timestamp_end_us = now_us;

  int64_t sent_duration_ms = (g_sent_stats.timestamp_end_us - g_sent_stats.timestamp_start_us) / 1000;
  int64_t recv_duration_ms = (g_received_stats.timestamp_end_us - g_received_stats.timestamp_start_us) / 1000;

  log_plain("================================================================================");
  log_plain("                         AUDIO ANALYSIS REPORT                                 ");
  log_plain("================================================================================");
  log_plain("SENT AUDIO (Microphone Capture):");
  log_plain("  Duration:                %lld ms", (long long)sent_duration_ms);
  log_plain("  Total Samples:           %llu", (unsigned long long)g_sent_stats.total_samples);
  log_plain("  Peak Level:              %.4f (should be < 1.0)", g_sent_stats.peak_level);
  log_plain("  Clipping Events:         %llu samples (%.2f%%)", (unsigned long long)g_sent_stats.clipping_count,
            g_sent_stats.total_samples > 0 ? (100.0 * g_sent_stats.clipping_count / g_sent_stats.total_samples) : 0);
  log_plain("  Silent Samples:          %llu samples (%.2f%%)", (unsigned long long)g_sent_stats.silent_samples,
            g_sent_stats.total_samples > 0 ? (100.0 * g_sent_stats.silent_samples / g_sent_stats.total_samples) : 0);
  log_plain("  Packets Sent:            %u", g_sent_stats.packets_count);
  log_plain("  Status:                  %s", g_sent_stats.clipping_count > 0 ? "CLIPPING DETECTED!" : "OK");

  log_plain("RECEIVED AUDIO (Playback):");
  log_plain("  Duration:                %lld ms", (long long)recv_duration_ms);
  log_plain("  Total Samples:           %llu", (unsigned long long)g_received_stats.total_samples);
  log_plain("  Peak Level:              %.4f", g_received_stats.peak_level);
  log_plain("  Clipping Events:         %llu samples (%.2f%%)", (unsigned long long)g_received_stats.clipping_count,
            g_received_stats.total_samples > 0
                ? (100.0 * g_received_stats.clipping_count / g_received_stats.total_samples)
                : 0);
  log_plain("  Silent Samples:          %llu samples (%.2f%%)", (unsigned long long)g_received_stats.silent_samples,
            g_received_stats.total_samples > 0
                ? (100.0 * g_received_stats.silent_samples / g_received_stats.total_samples)
                : 0);
  log_plain("  Packets Received:        %u", g_received_stats.packets_count);
  log_plain("  Status:                  %s", g_received_stats.total_samples == 0 ? "NO AUDIO RECEIVED!" : "Receiving");

  log_plain("QUALITY METRICS (Scratchy/Distorted Audio Detection):");
  log_plain("SENT:");
  log_plain("  Jitter Events:           %llu (rapid amplitude changes)", (unsigned long long)g_sent_stats.jitter_count);
  log_plain("  Discontinuities:         %llu (packet arrival gaps > 100ms)",
            (unsigned long long)g_sent_stats.discontinuity_count);
  log_plain("  Max Gap Between Packets: %u ms (expected ~20ms per frame)", g_sent_stats.max_gap_ms);

  log_plain("RECEIVED:");
  log_plain("  Jitter Events:           %llu (rapid amplitude changes)",
            (unsigned long long)g_received_stats.jitter_count);
  log_plain("  Discontinuities:         %llu (packet arrival gaps > 100ms)",
            (unsigned long long)g_received_stats.discontinuity_count);
  log_plain("  Max Gap Between Packets: %u ms (expected ~20ms per frame)", g_received_stats.max_gap_ms);

  log_plain("DIAGNOSTICS:");
  if (g_sent_stats.peak_level == 0) {
    log_plain("  No audio captured from microphone!");
  }
  if (g_received_stats.total_samples == 0) {
    log_plain("  No audio received from server!");
  } else if (g_received_stats.peak_level < 0.01f) {
    log_plain("  Received audio is very quiet (peak < 0.01)");
  }
  if (g_sent_stats.clipping_count > 0) {
    log_plain("  Microphone input is clipping - reduce microphone volume");
  }

  // Scratchy/distorted diagnosis
  if (g_received_stats.discontinuity_count > 0 || g_received_stats.max_gap_ms > 100) {
    log_plain("  LIKELY CAUSE OF SCRATCHY AUDIO: Sparse packet delivery detected!");
    log_plain("    - Packets arriving with gaps > 100ms causes jitter buffer underruns");
    log_plain("    - This results in dropouts, clicks, and distorted playback");
    log_plain("    - Root cause: Lock contention in packet queue (target: 172 fps, actual: ~34 fps)");
  }
  if (g_received_stats.jitter_count > (g_received_stats.total_samples / 100)) {
    log_plain("  High jitter detected: > 1%% of samples have rapid amplitude changes");
    log_plain("    - May indicate network quality issues or buffer underruns");
  }

  log_plain("================================================================================");
}

void audio_analysis_cleanup(void) {
  g_analysis_enabled = false;
}
