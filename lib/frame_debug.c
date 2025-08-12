#include "frame_debug.h"
#include "common.h"
#include <string.h>
#include <math.h>

// Global debug controls
bool g_frame_debug_enabled = false;
int g_frame_debug_verbosity = 1;

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

// Simple hash function for frame content comparison
uint32_t frame_debug_hash_content(const char *data, size_t size) {
  if (!data || size == 0)
    return 0;

  uint32_t hash = 2166136261U;                    // FNV-1a offset basis
  size_t hash_size = (size < 1024) ? size : 1024; // Hash first 1KB

  for (size_t i = 0; i < hash_size; i++) {
    hash ^= (uint8_t)data[i];
    hash *= 16777619U; // FNV-1a prime
  }

  return hash;
}

// Calculate time difference in milliseconds
double frame_debug_time_diff_ms(const struct timespec *start, const struct timespec *end) {
  double start_ms = start->tv_sec * 1000.0 + start->tv_nsec / 1000000.0;
  double end_ms = end->tv_sec * 1000.0 + end->tv_nsec / 1000000.0;
  return end_ms - start_ms;
}

/* ============================================================================
 * Frame Debug Tracker Implementation
 * ============================================================================ */

void frame_debug_init(frame_debug_tracker_t *tracker, const char *component_name) {
  if (!tracker)
    return;

  memset(tracker, 0, sizeof(*tracker));
  tracker->component_name = component_name;
  tracker->initialized = true;
  clock_gettime(CLOCK_MONOTONIC, &tracker->last_frame_time);

  if (g_frame_debug_verbosity >= 1) {
    log_info("Frame debug initialized for component: %s", component_name);
  }
}

void frame_debug_record_frame(frame_debug_tracker_t *tracker, const char *frame_data, size_t frame_size) {
  if (!tracker || !tracker->initialized || !g_frame_debug_enabled) {
    return;
  }

  struct timespec current_time;
  clock_gettime(CLOCK_MONOTONIC, &current_time);

  // Calculate timing
  double ms_since_last = 0.0;
  if (tracker->total_frames > 0) {
    ms_since_last = frame_debug_time_diff_ms(&tracker->last_frame_time, &current_time);
  }

  // Generate content hash
  uint32_t content_hash = frame_debug_hash_content(frame_data, frame_size);

  // EMPTY FRAME DETECTION: Check if frame is mostly empty/whitespace
  size_t non_whitespace_chars = 0;
  size_t newline_chars = 0;
  size_t total_printable = 0;

  for (size_t i = 0; i < frame_size && i < 1024; i++) { // Check first 1KB
    char c = frame_data[i];
    if (c == '\n') {
      newline_chars++;
    } else if (c != ' ' && c != '\t' && c != '\r' && c > 32 && c < 127) {
      non_whitespace_chars++;
      total_printable++;
    } else if (c >= 32 && c < 127) {
      total_printable++;
    }
  }

  double whitespace_ratio =
      (total_printable > 0) ? (double)(total_printable - non_whitespace_chars) / total_printable : 1.0;
  bool is_empty_frame = (non_whitespace_chars < 10 || whitespace_ratio > 0.95);

  if (is_empty_frame && g_frame_debug_verbosity >= 1) {
    log_warn("%s: EMPTY/WHITESPACE FRAME detected - size=%zu, non-ws=%zu, newlines=%zu, ws_ratio=%.1f%%",
             tracker->component_name, frame_size, non_whitespace_chars, newline_chars, whitespace_ratio * 100.0);
  }

  // Detect duplicates
  bool is_duplicate = (tracker->total_frames > 0 && content_hash == tracker->last_content_hash);
  if (is_duplicate) {
    tracker->duplicate_frames++;
  }

  // Detect slow frames
  if (ms_since_last > FRAME_DEBUG_TIMING_THRESHOLD_MS) {
    tracker->slow_frames++;
    if (g_frame_debug_verbosity >= 2) {
      log_warn("%s: Slow frame detected - %.1fms since last (threshold: %dms)", tracker->component_name, ms_since_last,
               FRAME_DEBUG_TIMING_THRESHOLD_MS);
    }
  }

  // Store frame info in circular buffer
  frame_debug_entry_t *entry = &tracker->history[tracker->history_index];
  entry->frame_id = tracker->total_frames;
  entry->timestamp = current_time;
  entry->content_hash = content_hash;
  entry->frame_size = frame_size;
  entry->is_duplicate = is_duplicate;
  entry->ms_since_last = ms_since_last;

  // Sample frame content for debugging
  size_t sample_size = (frame_size < FRAME_DEBUG_CONTENT_SAMPLE_SIZE) ? frame_size : FRAME_DEBUG_CONTENT_SAMPLE_SIZE;
  if (frame_data && sample_size > 0) {
    memcpy(entry->content_sample, frame_data, sample_size);
    if (sample_size < FRAME_DEBUG_CONTENT_SAMPLE_SIZE) {
      entry->content_sample[sample_size] = '\0';
    }
  }

  // Update tracker state
  tracker->history_index = (tracker->history_index + 1) % FRAME_DEBUG_HISTORY_SIZE;
  tracker->total_frames++;
  tracker->last_frame_time = current_time;
  tracker->last_content_hash = content_hash;

  // Verbose logging
  if (g_frame_debug_verbosity >= 3) {
    log_debug("%s: Frame #%llu, size=%zu, hash=0x%x, duplicate=%s, dt=%.1fms", tracker->component_name,
              (unsigned long long)tracker->total_frames, frame_size, content_hash, is_duplicate ? "YES" : "NO",
              ms_since_last);
  }

  // Periodic issue detection
  if (tracker->total_frames % 100 == 0) {
    frame_debug_detect_issues(tracker);
  }
}

void frame_debug_detect_issues(frame_debug_tracker_t *tracker) {
  if (!tracker || !tracker->initialized || tracker->total_frames == 0) {
    return;
  }

  double duplicate_rate = (double)tracker->duplicate_frames * 100.0 / tracker->total_frames;
  double slow_rate = (double)tracker->slow_frames * 100.0 / tracker->total_frames;

  // Issue detection thresholds
  const double DUPLICATE_THRESHOLD = 50.0; // 50% duplicates = problem
  const double SLOW_THRESHOLD = 5.0;       // 5% slow frames = problem

  bool has_issues = false;

  if (duplicate_rate > DUPLICATE_THRESHOLD) {
    log_warn("%s: HIGH DUPLICATE RATE: %.1f%% (%llu/%llu frames)", tracker->component_name, duplicate_rate,
             (unsigned long long)tracker->duplicate_frames, (unsigned long long)tracker->total_frames);
    has_issues = true;
  }

  if (slow_rate > SLOW_THRESHOLD) {
    log_warn("%s: HIGH SLOW FRAME RATE: %.1f%% (%llu/%llu frames)", tracker->component_name, slow_rate,
             (unsigned long long)tracker->slow_frames, (unsigned long long)tracker->total_frames);
    has_issues = true;
  }

  // Check for frame sequence gaps in recent history
  if (tracker->total_frames >= FRAME_DEBUG_HISTORY_SIZE) {
    size_t gaps = 0;
    size_t start_idx = tracker->history_index;

    for (size_t i = 1; i < FRAME_DEBUG_HISTORY_SIZE; i++) {
      size_t curr_idx = (start_idx + i) % FRAME_DEBUG_HISTORY_SIZE;
      frame_debug_entry_t *curr = &tracker->history[curr_idx];

      // Check for timing gaps (>200ms)
      if (curr->ms_since_last > 200.0) {
        gaps++;
      }
    }

    if (gaps > 5) { // More than 5 gaps in last 100 frames
      log_warn("%s: FRAME TIMING GAPS: %zu gaps in last %d frames", tracker->component_name, gaps,
               FRAME_DEBUG_HISTORY_SIZE);
      has_issues = true;
    }
  }

  if (has_issues && g_frame_debug_verbosity >= 2) {
    frame_debug_print_stats(tracker);
  }
}

void frame_debug_print_stats(frame_debug_tracker_t *tracker) {
  if (!tracker || !tracker->initialized) {
    return;
  }

  double duplicate_rate =
      (tracker->total_frames > 0) ? (double)tracker->duplicate_frames * 100.0 / tracker->total_frames : 0.0;
  double slow_rate = (tracker->total_frames > 0) ? (double)tracker->slow_frames * 100.0 / tracker->total_frames : 0.0;

  log_info("=== Frame Debug Stats: %s ===", tracker->component_name);
  log_info("Total frames: %llu", (unsigned long long)tracker->total_frames);
  log_info("Duplicate frames: %llu (%.1f%%)", (unsigned long long)tracker->duplicate_frames, duplicate_rate);
  log_info("Slow frames: %llu (%.1f%%)", (unsigned long long)tracker->slow_frames, slow_rate);
  log_info("Dropped frames: %llu", (unsigned long long)tracker->dropped_frames);

  // Show recent frame timing
  if (tracker->total_frames >= 10) {
    log_info("Recent frame timings (last 10 frames):");
    size_t start_idx = (tracker->history_index + FRAME_DEBUG_HISTORY_SIZE - 10) % FRAME_DEBUG_HISTORY_SIZE;

    for (int i = 0; i < 10; i++) {
      size_t idx = (start_idx + i) % FRAME_DEBUG_HISTORY_SIZE;
      frame_debug_entry_t *entry = &tracker->history[idx];

      if (entry->frame_id > 0) {
        log_info("  Frame #%llu: %.1fms, size=%zu, hash=0x%x%s", (unsigned long long)entry->frame_id,
                 entry->ms_since_last, entry->frame_size, entry->content_hash, entry->is_duplicate ? " [DUP]" : "");
      }
    }
  }
}