#pragma once

#include <stdint.h>
#include "platform/abstraction.h"

/* ============================================================================
 * Performance Statistics
 * ============================================================================
 */
typedef struct {
  uint64_t frames_captured;
  uint64_t frames_sent;
  uint64_t frames_dropped;
  uint64_t bytes_sent;
  double avg_capture_fps;
  double avg_send_fps;
} server_stats_t;

// Statistics thread function
void *stats_logger_thread(void *arg);

// Global statistics
extern server_stats_t g_stats;
extern mutex_t g_stats_mutex;

// Statistics functions
int stats_init(void);
void stats_cleanup(void);
void update_server_stats(void);
void log_server_stats(void);
