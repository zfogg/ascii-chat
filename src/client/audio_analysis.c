/**
 * @file client/audio_analysis.c
 * @ingroup client_audio
 * @brief Audio Analysis Implementation
 */

#include "audio_analysis.h"
#include "common.h"
#include "logging.h"
#include "wav_writer.h"
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

static audio_analysis_stats_t g_sent_stats;
static audio_analysis_stats_t g_received_stats;
static bool g_analysis_enabled = false;

// WAV file writers for audio debugging
static wav_writer_t *g_sent_wav = NULL;
static wav_writer_t *g_received_wav = NULL;

// For jitter detection (rapid amplitude changes)
static float g_sent_last_sample = 0.0f;
static float g_received_last_sample = 0.0f;

// For discontinuity detection (packet arrival gaps)
static int64_t g_sent_last_packet_time_us = 0;
static int64_t g_received_last_packet_time_us = 0;

// For RMS and energy tracking
static float g_sent_rms_accumulator = 0.0f;
static float g_received_rms_accumulator = 0.0f;
static uint64_t g_sent_rms_sample_count = 0;
static uint64_t g_received_rms_sample_count = 0;

// For silence burst detection
static uint64_t g_sent_silence_burst = 0;
static uint64_t g_received_silence_burst = 0;
static uint64_t g_sent_max_silence_burst = 0;
static uint64_t g_received_max_silence_burst = 0;

// For consecutive low-energy detection
static uint64_t g_received_low_energy_samples = 0; // Samples < 0.05 (very quiet)
static uint64_t g_received_below_noise_floor = 0;  // Samples < 0.001 (noise floor)

// For clipping and artifact detection
static uint64_t g_sent_clipping_samples = 0;      // Samples exceeding ±1.0
static uint64_t g_received_clipping_samples = 0;  // Samples exceeding ±1.0
static uint64_t g_sent_sharp_transitions = 0;     // Sudden amplitude jumps (potential clicks/pops)
static uint64_t g_received_sharp_transitions = 0; // Sudden amplitude jumps

// For dynamic range and musicality detection
static float g_sent_sample_variance = 0.0f;
static float g_received_sample_variance = 0.0f;
static float g_sent_mean = 0.0f;
static float g_received_mean = 0.0f;
static uint64_t g_sent_transition_samples = 0;
static uint64_t g_received_transition_samples = 0;

// For frame-based analysis (Opus frames are 960 samples = 20ms)
#define FRAME_SIZE 960
static float g_sent_frame_rms_sum = 0.0f;
static float g_sent_frame_rms_sq_sum = 0.0f;
static uint64_t g_sent_frame_count = 0;
static float g_received_frame_rms_sum = 0.0f;
static float g_received_frame_rms_sq_sum = 0.0f;
static uint64_t g_received_frame_count = 0;

// For zero-crossing detection (indicates spectral content)
static uint64_t g_sent_zero_crossings = 0;
static uint64_t g_received_zero_crossings = 0;

// For stuttering/gap detection (periodic silence bursts)
#define MAX_GAP_SAMPLES 100
static uint32_t g_received_gap_intervals_ms[MAX_GAP_SAMPLES]; // Time between silence bursts
static uint32_t g_received_gap_count = 0;                     // Number of gaps detected
static uint64_t g_received_silence_start_sample = 0;          // When silence started
static uint64_t g_received_last_silence_end_sample = 0;       // When last silence ended

// Direct packet timing tracking for stuttering detection
#define MAX_PACKET_SAMPLES 200
static struct timespec g_received_packet_times[MAX_PACKET_SAMPLES]; // When each packet was received
static uint32_t g_received_packet_times_count = 0;                  // Number of packets tracked
static uint32_t g_received_packet_sizes[MAX_PACKET_SAMPLES];        // Size of each packet in bytes
static uint64_t g_received_total_audio_samples = 0;                 // Total decoded audio samples

int audio_analysis_init(void) {
  SAFE_MEMSET(&g_sent_stats, sizeof(g_sent_stats), 0, sizeof(g_sent_stats));
  SAFE_MEMSET(&g_received_stats, sizeof(g_received_stats), 0, sizeof(g_received_stats));

  // Reset stuttering/gap tracking
  SAFE_MEMSET(g_received_gap_intervals_ms, sizeof(g_received_gap_intervals_ms), 0, sizeof(g_received_gap_intervals_ms));
  g_received_gap_count = 0;
  g_received_silence_start_sample = 0;
  g_received_last_silence_end_sample = 0;
  SAFE_MEMSET(g_received_packet_times, sizeof(g_received_packet_times), 0, sizeof(g_received_packet_times));
  g_received_packet_times_count = 0;
  SAFE_MEMSET(g_received_packet_sizes, sizeof(g_received_packet_sizes), 0, sizeof(g_received_packet_sizes));
  g_received_total_audio_samples = 0;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  int64_t now_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

  g_sent_stats.timestamp_start_us = now_us;
  g_received_stats.timestamp_start_us = now_us;

  g_sent_last_sample = 0.0f;
  g_received_last_sample = 0.0f;
  g_sent_last_packet_time_us = now_us;
  g_received_last_packet_time_us = now_us;

  // Initialize WAV file dumping if enabled
  if (wav_dump_enabled()) {
    g_sent_wav = wav_writer_open("/tmp/sent_audio.wav", 48000, 1);
    g_received_wav = wav_writer_open("/tmp/received_audio.wav", 48000, 1);
    if (g_sent_wav) {
      log_info("Dumping sent audio to /tmp/sent_audio.wav");
    }
    if (g_received_wav) {
      log_info("Dumping received audio to /tmp/received_audio.wav");
    }
  }

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

  // Track clipping (samples > 1.0) - indicates distortion
  if (abs_sample > 1.0f) {
    g_sent_stats.clipping_count++;
    g_sent_clipping_samples++;
  }

  // Detect sharp transitions (sudden amplitude jumps > 0.3) - indicates clicks/pops
  float amp_change = fabsf(sample - g_sent_last_sample);
  if (amp_change > 0.3f) {
    g_sent_sharp_transitions++;
  }
  g_sent_transition_samples++;

  // Accumulate for mean calculation
  g_sent_mean += sample;

  // Detect zero crossings (waveform crossing zero) - indicates spectral content
  static float g_sent_prev_sample = 0.0f;
  if ((g_sent_prev_sample > 0 && sample < 0) || (g_sent_prev_sample < 0 && sample > 0)) {
    g_sent_zero_crossings++;
  }
  g_sent_prev_sample = sample;

  // Track silence (very low level)
  if (abs_sample < 0.001f) {
    g_sent_stats.silent_samples++;
    g_sent_silence_burst++;
  } else {
    // Silence ended - track max burst length
    if (g_sent_silence_burst > g_sent_max_silence_burst) {
      g_sent_max_silence_burst = g_sent_silence_burst;
    }
    g_sent_silence_burst = 0;
  }

  // Detect jitter: rapid amplitude changes > 0.5 between consecutive samples
  float delta = fabsf(sample - g_sent_last_sample);
  if (delta > 0.5f) {
    g_sent_stats.jitter_count++;
  }
  g_sent_last_sample = sample;

  // Accumulate for RMS calculation
  g_sent_rms_accumulator += sample * sample;
  g_sent_rms_sample_count++;

  // Write to WAV file if enabled
  if (g_sent_wav) {
    wav_writer_write(g_sent_wav, &sample, 1);
  }
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

  // Track clipping (samples > 1.0) - indicates distortion
  if (abs_sample > 1.0f) {
    g_received_stats.clipping_count++;
    g_received_clipping_samples++;
  }

  // Detect sharp transitions (sudden amplitude jumps > 0.3) - indicates clicks/pops/artifacts
  float amp_change = fabsf(sample - g_received_last_sample);
  if (amp_change > 0.3f) {
    g_received_sharp_transitions++;
  }
  g_received_transition_samples++;

  // Accumulate for mean calculation
  g_received_mean += sample;

  // Detect zero crossings (waveform crossing zero) - indicates spectral content
  static float g_received_prev_sample = 0.0f;
  if ((g_received_prev_sample > 0 && sample < 0) || (g_received_prev_sample < 0 && sample > 0)) {
    g_received_zero_crossings++;
  }
  g_received_prev_sample = sample;

  // Track silence and low-energy audio
  if (abs_sample < 0.001f) {
    g_received_stats.silent_samples++;
    g_received_silence_burst++;
    g_received_below_noise_floor++;

    // Track when silence started
    if (g_received_silence_burst == 1) {
      g_received_silence_start_sample = g_received_stats.total_samples;
    }
  } else {
    // Silence ended - track gap interval and max burst length
    if (g_received_silence_burst > 0) {
      // Calculate time gap between end of last silence and start of this one
      if (g_received_last_silence_end_sample > 0) {
        uint64_t samples_between = g_received_silence_start_sample - g_received_last_silence_end_sample;
        uint32_t ms_between = (uint32_t)(samples_between * 1000 / 48000); // Convert samples to ms at 48kHz

        // Track the gap interval if we have room
        if (g_received_gap_count < MAX_GAP_SAMPLES) {
          g_received_gap_intervals_ms[g_received_gap_count++] = ms_between;
        }
      }

      g_received_last_silence_end_sample = g_received_stats.total_samples;

      // Track max burst length
      if (g_received_silence_burst > g_received_max_silence_burst) {
        g_received_max_silence_burst = g_received_silence_burst;
      }
    }
    g_received_silence_burst = 0;
  }

  // Track very quiet audio (< 0.05 amplitude) which contributes to muddy/quiet perception
  if (abs_sample < 0.05f) {
    g_received_low_energy_samples++;
  }

  // Detect jitter: rapid amplitude changes > 0.5 between consecutive samples
  float delta = fabsf(sample - g_received_last_sample);
  if (delta > 0.5f) {
    g_received_stats.jitter_count++;
  }
  g_received_last_sample = sample;

  // Accumulate for RMS calculation
  g_received_rms_accumulator += sample * sample;
  g_received_rms_sample_count++;

  // Write to WAV file if enabled
  if (g_received_wav) {
    wav_writer_write(g_received_wav, &sample, 1);
  }
}

void audio_analysis_track_received_packet(size_t size) {
  if (!g_analysis_enabled)
    return;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  int64_t now_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

  // Track packet timing for stuttering detection
  if (g_received_packet_times_count < MAX_PACKET_SAMPLES) {
    g_received_packet_times[g_received_packet_times_count++] = ts;
  }

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

  // Calculate RMS levels
  float sent_rms = 0.0f;
  float recv_rms = 0.0f;
  if (g_sent_rms_sample_count > 0) {
    sent_rms = sqrtf(g_sent_rms_accumulator / g_sent_rms_sample_count);
  }
  if (g_received_rms_sample_count > 0) {
    recv_rms = sqrtf(g_received_rms_accumulator / g_received_rms_sample_count);
  }

  log_plain("================================================================================");
  log_plain("                         AUDIO ANALYSIS REPORT                                 ");
  log_plain("================================================================================");
  log_plain("SENT AUDIO (Microphone Capture):");
  log_plain("  Duration:                %lld ms", (long long)sent_duration_ms);
  log_plain("  Total Samples:           %llu", (unsigned long long)g_sent_stats.total_samples);
  log_plain("  Peak Level:              %.4f (should be < 1.0)", g_sent_stats.peak_level);
  log_plain("  RMS Level:               %.4f (audio energy/loudness)", sent_rms);
  log_plain("  Clipping Events:         %llu samples (%.2f%%)", (unsigned long long)g_sent_stats.clipping_count,
            g_sent_stats.total_samples > 0 ? (100.0 * g_sent_stats.clipping_count / g_sent_stats.total_samples) : 0);
  log_plain("  Silent Samples:          %llu samples (%.2f%%)", (unsigned long long)g_sent_stats.silent_samples,
            g_sent_stats.total_samples > 0 ? (100.0 * g_sent_stats.silent_samples / g_sent_stats.total_samples) : 0);
  if (g_sent_max_silence_burst > 0) {
    log_plain("  Max Silence Burst:       %llu samples", (unsigned long long)g_sent_max_silence_burst);
  }
  log_plain("  Packets Sent:            %u", g_sent_stats.packets_count);
  log_plain("  Status:                  %s", g_sent_stats.clipping_count > 0 ? "CLIPPING DETECTED!" : "OK");

  log_plain("RECEIVED AUDIO (Playback):");
  log_plain("  Duration:                %lld ms", (long long)recv_duration_ms);
  log_plain("  Total Samples:           %llu", (unsigned long long)g_received_stats.total_samples);
  log_plain("  Peak Level:              %.4f", g_received_stats.peak_level);
  log_plain("  RMS Level:               %.4f (audio energy/loudness)", recv_rms);
  log_plain("  Clipping Events:         %llu samples (%.2f%%)", (unsigned long long)g_received_stats.clipping_count,
            g_received_stats.total_samples > 0
                ? (100.0 * g_received_stats.clipping_count / g_received_stats.total_samples)
                : 0);
  log_plain("  Silent Samples:          %llu samples (%.2f%%)", (unsigned long long)g_received_stats.silent_samples,
            g_received_stats.total_samples > 0
                ? (100.0 * g_received_stats.silent_samples / g_received_stats.total_samples)
                : 0);
  if (g_received_max_silence_burst > 0) {
    log_plain("  Max Silence Burst:       %llu samples", (unsigned long long)g_received_max_silence_burst);
  }
  double low_energy_pct =
      g_received_stats.total_samples > 0 ? (100.0 * g_received_low_energy_samples / g_received_stats.total_samples) : 0;
  log_plain("  Very Quiet Samples:      %llu samples (%.1f%%) [amplitude < 0.05]",
            (unsigned long long)g_received_low_energy_samples, low_energy_pct);
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
    log_plain("  ⚠️  Received audio is very quiet (peak < 0.01)");
  }
  if (g_sent_stats.clipping_count > 0) {
    log_plain("  Microphone input is clipping - reduce microphone volume");
  }

  // Audio quality diagnostics
  if (recv_rms < 0.005f) {
    log_plain("  ⚠️  CRITICAL: Received audio RMS is extremely low (%.6f) - barely audible!", recv_rms);
  } else if (recv_rms < 0.02f) {
    log_plain("  ⚠️  WARNING: Received audio RMS is low (%.6f) - may sound quiet or muddy", recv_rms);
  }

  // Silence analysis
  double received_silence_pct = g_received_stats.total_samples > 0
                                    ? (100.0 * g_received_stats.silent_samples / g_received_stats.total_samples)
                                    : 0;

  if (received_silence_pct > 30.0) {
    log_plain("  ⚠️  SCRATCHY AUDIO DETECTED: Too much silence in received audio!");
    log_plain("    - Silence: %.1f%% of received samples (should be < 10%%)", received_silence_pct);
    log_plain("    - Max silence burst: %llu samples", (unsigned long long)g_received_max_silence_burst);
    log_plain("    - This creates jittery/choppy playback between audio bursts");
  } else if (received_silence_pct > 15.0) {
    log_plain("  ⚠️  WARNING: Moderate silence detected (%.1f%%)", received_silence_pct);
  }

  // Sharp transition analysis (clicks/pops)
  double sent_sharp_pct =
      g_sent_transition_samples > 0 ? (100.0 * g_sent_sharp_transitions / g_sent_transition_samples) : 0;
  double recv_sharp_pct =
      g_received_transition_samples > 0 ? (100.0 * g_received_sharp_transitions / g_received_transition_samples) : 0;

  // Zero crossing rate analysis (spectral content)
  // Music: 1-5%, Speech: 5-15%, Static/Noise: 15-50%
  double sent_zero_cross_pct =
      g_sent_stats.total_samples > 0 ? (100.0 * g_sent_zero_crossings / g_sent_stats.total_samples) : 0;
  double recv_zero_cross_pct =
      g_received_stats.total_samples > 0 ? (100.0 * g_received_zero_crossings / g_received_stats.total_samples) : 0;

  log_plain("WAVEFORM ANALYSIS (Is it clean music or corrupted/static?):");
  log_plain("SENT AUDIO:");
  log_plain("  Zero crossings: %.2f%% of samples (music: 1-5%%, noise: 15-50%%)", sent_zero_cross_pct);
  log_plain("  Sharp transitions (clicks/pops): %.2f%% of samples", sent_sharp_pct);
  log_plain("  Clipping samples: %llu (%.3f%%)", (unsigned long long)g_sent_clipping_samples,
            g_sent_stats.total_samples > 0 ? (100.0 * g_sent_clipping_samples / g_sent_stats.total_samples) : 0);

  log_plain("RECEIVED AUDIO:");
  log_plain("  Zero crossings: %.2f%% of samples (music: 1-5%%, noise: 15-50%%)", recv_zero_cross_pct);
  log_plain("  Sharp transitions (clicks/pops): %.2f%% of samples", recv_sharp_pct);
  log_plain("  Clipping samples: %llu (%.3f%%)", (unsigned long long)g_received_clipping_samples,
            g_received_stats.total_samples > 0 ? (100.0 * g_received_clipping_samples / g_received_stats.total_samples)
                                               : 0);
  log_plain("  Zero crossing increase: %.2f%% higher than sent (indicates corruption)",
            recv_zero_cross_pct - sent_zero_cross_pct);

  // Musicality verdict
  log_plain("SOUND QUALITY VERDICT:");
  if (recv_zero_cross_pct > 10.0) {
    log_plain("  ⚠️  SOUNDS LIKE STATIC/DISTORTED: Excessive zero crossings (%.2f%%) = high frequency noise",
              recv_zero_cross_pct);
    log_plain("     Increase from sent: %.2f%% (waveform corruption detected)",
              recv_zero_cross_pct - sent_zero_cross_pct);
    log_plain("     Likely causes: Opus codec artifacts, jitter buffer issues, or packet delivery gaps");
  } else if (recv_zero_cross_pct - sent_zero_cross_pct > 3.0) {
    log_plain("  ⚠️  SOUNDS CORRUPTED: Zero crossing rate increased by %.2f%% (should be ±0.5%%)",
              recv_zero_cross_pct - sent_zero_cross_pct);
    log_plain("     Indicates waveform distortion from network/processing artifacts");
  } else if (recv_sharp_pct > 2.0) {
    log_plain("  ⚠️  SOUNDS LIKE STATIC: High click/pop rate (%.2f%%) indicates audio artifacts", recv_sharp_pct);
    log_plain("     Likely causes: Packet loss, jitter buffer issues, or frame discontinuities");
  } else if (g_received_clipping_samples > (g_received_stats.total_samples / 1000)) {
    log_plain("  ⚠️  SOUNDS DISTORTED: Significant clipping detected (%.3f%%)",
              100.0 * g_received_clipping_samples / g_received_stats.total_samples);
    log_plain("     Likely causes: AGC too aggressive, gain too high, or codec compression artifacts");
  } else if (low_energy_pct > 50.0 && recv_rms < 0.05f) {
    log_plain("  ⚠️  SOUNDS MUDDY/QUIET: Over 50%% very quiet samples + low RMS");
    log_plain("     Audio may sound unclear or like background noise rather than music");
  } else if (received_silence_pct > 10.0) {
    log_plain("  ⚠️  SOUNDS SCRATCHY: Excessive silence (%.1f%%) causes dropouts", received_silence_pct);
  } else if (recv_rms > 0.08f && recv_zero_cross_pct < 6.0 && recv_sharp_pct < 1.0 &&
             g_received_clipping_samples == 0) {
    log_plain("  ✓ SOUNDS LIKE MUSIC: Good RMS (%.4f), clean waveform (%.2f%% zero crossings), minimal artifacts",
              recv_rms, recv_zero_cross_pct);
    log_plain("     Audio quality acceptable for communication");
  } else {
    log_plain("  ? BORDERLINE: Check specific metrics above");
  }

  // Low energy audio analysis
  if (low_energy_pct > 50.0) {
    log_plain("  ⚠️  WARNING: Over 50%% of received samples are very quiet (< 0.05 amplitude)");
    log_plain("    - This makes audio sound muddy, unclear, or hard to understand");
    log_plain("    - Caused by: Mixing other clients' audio with your own at wrong levels");
  }

  // Stuttering/periodic gap detection using packet inter-arrival times
  if (g_received_packet_times_count >= 5) {
    uint32_t inter_arrival_times_ms[MAX_PACKET_SAMPLES - 1];
    uint32_t inter_arrival_count = 0;
    uint32_t min_interval_ms = 0xFFFFFFFF;
    uint32_t max_interval_ms = 0;
    uint64_t sum_intervals_ms = 0;
    uint32_t intervals_around_50ms = 0; // Count intervals ~40-60ms

    // Calculate inter-packet arrival times
    for (uint32_t i = 1; i < g_received_packet_times_count; i++) {
      struct timespec *prev = &g_received_packet_times[i - 1];
      struct timespec *curr = &g_received_packet_times[i];

      int64_t prev_us = (int64_t)prev->tv_sec * 1000000 + prev->tv_nsec / 1000;
      int64_t curr_us = (int64_t)curr->tv_sec * 1000000 + curr->tv_nsec / 1000;
      uint32_t gap_ms = (uint32_t)((curr_us - prev_us) / 1000);

      inter_arrival_times_ms[inter_arrival_count++] = gap_ms;
      if (gap_ms < min_interval_ms)
        min_interval_ms = gap_ms;
      if (gap_ms > max_interval_ms)
        max_interval_ms = gap_ms;
      sum_intervals_ms += gap_ms;

      // Check if interval is ~50ms (within 15ms tolerance for network jitter)
      if (gap_ms >= 35 && gap_ms <= 70) {
        intervals_around_50ms++;
      }
    }

    uint32_t avg_interval_ms = (uint32_t)(sum_intervals_ms / inter_arrival_count);
    uint32_t interval_consistency = (intervals_around_50ms * 100) / inter_arrival_count;

    // Calculate how much audio is in each packet
    // Total decoded samples / number of packets = average samples per packet
    // At 48kHz, 960 samples = 1 Opus frame = 20ms
    double avg_samples_per_packet =
        g_received_stats.total_samples > 0 ? (double)g_received_stats.total_samples / inter_arrival_count : 0;
    double frames_per_packet = avg_samples_per_packet / 960.0; // 960 samples = 1 frame @ 48kHz
    double ms_audio_per_packet = frames_per_packet * 20.0;     // 20ms per frame

    // Detect if stuttering is periodic (consistent ~50ms intervals)
    if (intervals_around_50ms >= (inter_arrival_count * 2 / 3)) {
      // More than 66% of packets are ~50ms apart - clear periodic stuttering
      log_plain("  🔴 PERIODIC STUTTERING DETECTED: Server sends packets every ~%u ms (should be ~20ms)!",
                avg_interval_ms);
      log_plain("    - Packet inter-arrival: %u-%u ms (avg: %u ms)", min_interval_ms, max_interval_ms, avg_interval_ms);
      log_plain("    - %u/%u packets (~%u%%) are ~50ms apart (CLEAR STUTTERING PATTERN)", intervals_around_50ms,
                inter_arrival_count, interval_consistency);

      log_plain("    - PACKET ANALYSIS:");
      log_plain("      - Total audio samples: %llu over %u packets", (unsigned long long)g_received_stats.total_samples,
                inter_arrival_count);
      log_plain("      - Avg samples per packet: %.0f (= %.2f Opus frames = %.1f ms)", avg_samples_per_packet,
                frames_per_packet, ms_audio_per_packet);

      if (frames_per_packet < 1.5) {
        log_plain("      - ❌ PROBLEM: Each packet contains < 1.5 frames (should be 2-3 frames!)");
        log_plain("      - With only %.1f frames per packet arriving every %u ms, there are gaps between chunks",
                  frames_per_packet, avg_interval_ms);
        log_plain("      - Audio plays for ~%.0f ms, then %u ms gap, then plays again", ms_audio_per_packet,
                  avg_interval_ms - (uint32_t)ms_audio_per_packet);
      } else if (frames_per_packet > 2.5) {
        log_plain("      - ✓ Packets contain %.1f frames (~%.0f ms audio each)", frames_per_packet,
                  ms_audio_per_packet);
        log_plain("      - Should play smoothly if jitter buffer is large enough");
        log_plain("      - If still stuttering, issue is jitter buffer depth or timing precision");
      } else {
        log_plain("      - Packets contain %.1f frames (~%.0f ms)", frames_per_packet, ms_audio_per_packet);
        log_plain("      - Borderline: buffer needs to hold %.0f ms to bridge %.u ms gap", ms_audio_per_packet,
                  avg_interval_ms - (uint32_t)ms_audio_per_packet);
      }
    } else if (avg_interval_ms > 30) {
      log_plain("  ⚠️  AUDIO DELIVERY INCONSISTENCY: Server packets arrive every ~%u ms (expected ~20ms)",
                avg_interval_ms);
      log_plain("    - Interval range: %u-%u ms", min_interval_ms, max_interval_ms);
      log_plain("    - This causes dropouts and buffering issues");
    }
  }

  // Packet delivery gaps
  if (g_received_stats.max_gap_ms > 40) {
    log_plain("  ⚠️  DISTORTION DETECTED: Packet delivery gaps too large!");
    log_plain("    - Max gap: %u ms (should be ~20ms for smooth audio)", g_received_stats.max_gap_ms);
    if (g_received_stats.max_gap_ms > 80) {
      log_plain("    - SEVERE: Gaps > 80ms cause severe distortion and dropouts");
    } else if (g_received_stats.max_gap_ms > 50) {
      log_plain("    - Gaps > 50ms cause noticeable distortion");
    }
  }
  if (g_received_stats.discontinuity_count > 0) {
    log_plain("  Packet delivery discontinuities: %llu gaps > 100ms detected",
              (unsigned long long)g_received_stats.discontinuity_count);
  }
  if (g_received_stats.jitter_count > (g_received_stats.total_samples / 100)) {
    log_plain("  High jitter detected: > 1%% of samples have rapid amplitude changes");
    log_plain("    - May indicate buffer underruns from sparse packet delivery");
  }

  log_plain("================================================================================");
}

void audio_analysis_cleanup(void) {
  g_analysis_enabled = false;

  // Close WAV files if they were open
  if (g_sent_wav) {
    wav_writer_close(g_sent_wav);
    g_sent_wav = NULL;
    log_info("Closed sent audio WAV file");
  }
  if (g_received_wav) {
    wav_writer_close(g_received_wav);
    g_received_wav = NULL;
    log_info("Closed received audio WAV file");
  }
}
