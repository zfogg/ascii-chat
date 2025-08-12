#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/*
 * Frame Debugging Utilities
 *
 * Tools to debug frame flickering and timing issues by:
 * - Tracking frame sequences and gaps
 * - Measuring timing between frames
 * - Detecting duplicate or missing frames
 * - Logging frame content changes
 */

// Frame debugging configuration
#define FRAME_DEBUG_HISTORY_SIZE 100
#define FRAME_DEBUG_CONTENT_SAMPLE_SIZE 64  // First N chars to compare
#define FRAME_DEBUG_TIMING_THRESHOLD_MS 100 // Flag frames slower than this

// Frame timing and sequence tracking
typedef struct {
  uint64_t frame_id;
  struct timespec timestamp;
  uint32_t content_hash;
  size_t frame_size;
  char content_sample[FRAME_DEBUG_CONTENT_SAMPLE_SIZE];
  bool is_duplicate;
  double ms_since_last;
} frame_debug_entry_t;

// Frame debug tracker for a specific component
typedef struct {
  const char *component_name;
  frame_debug_entry_t history[FRAME_DEBUG_HISTORY_SIZE];
  size_t history_index;
  uint64_t total_frames;
  uint64_t duplicate_frames;
  uint64_t dropped_frames;
  uint64_t slow_frames;
  struct timespec last_frame_time;
  uint32_t last_content_hash;
  bool initialized;
} frame_debug_tracker_t;

// Debug tracker functions
void frame_debug_init(frame_debug_tracker_t *tracker, const char *component_name);
void frame_debug_record_frame(frame_debug_tracker_t *tracker, const char *frame_data, size_t frame_size);
void frame_debug_print_stats(frame_debug_tracker_t *tracker);
void frame_debug_detect_issues(frame_debug_tracker_t *tracker);

// Utility functions
uint32_t frame_debug_hash_content(const char *data, size_t size);
double frame_debug_time_diff_ms(const struct timespec *start, const struct timespec *end);

// Global debug controls
extern bool g_frame_debug_enabled;
extern int g_frame_debug_verbosity; // 0=off, 1=stats only, 2=all frames, 3=verbose
