#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Memory Buffer Pool System
 *
 * Provides pre-allocated memory buffers in different size classes
 * to reduce malloc/free overhead in high-throughput scenarios.
 *
 * Used by packet_queue to efficiently manage packet data buffers.
 */

// Buffer sizes we commonly allocate (based on memory report analysis)
#define BUFFER_POOL_SMALL_SIZE 1024     // Audio packets (1KB)
#define BUFFER_POOL_MEDIUM_SIZE 65536   // Small video frames (64KB)
#define BUFFER_POOL_LARGE_SIZE 262144   // Large video frames (256KB)
#define BUFFER_POOL_XLARGE_SIZE 1310720 // Extra large frames (1.25MB)

// Number of buffers to pre-allocate per size class
#define BUFFER_POOL_SMALL_COUNT 128  // 128KB total for audio
#define BUFFER_POOL_MEDIUM_COUNT 128 // 8MB total for small frames
#define BUFFER_POOL_LARGE_COUNT 16   // 4MB total for large frames
#define BUFFER_POOL_XLARGE_COUNT 32  // Pre-allocate 32 × 1.25MB = 40MB for large frames

// Single buffer in the pool
typedef struct buffer_node {
  void *data;               // The actual buffer
  size_t size;              // Size of this buffer
  struct buffer_node *next; // Next free buffer in list
  bool in_use;              // Debug: track if buffer is in use
} buffer_node_t;

// Buffer pool for a specific size class
typedef struct buffer_pool {
  buffer_node_t *free_list; // Stack of free buffers
  buffer_node_t *nodes;     // Pre-allocated array of buffer nodes
  void *memory_block;       // Single allocation for all buffers
  size_t buffer_size;       // Size of each buffer in this pool
  size_t pool_size;         // Number of buffers in pool
  size_t used_count;        // Number currently in use
  // Statistics
  uint64_t hits;                  // Successful allocations from pool
  uint64_t misses;                // Had to fallback to malloc
  uint64_t returns;               // Successful returns to pool
  uint64_t peak_used;             // Peak number of buffers used
  uint64_t total_bytes_allocated; // Total bytes served (for analysis)
} buffer_pool_t;

// Multi-size buffer pool manager
typedef struct data_buffer_pool {
  buffer_pool_t *small_pool;  // Pool for small buffers (audio)
  buffer_pool_t *medium_pool; // Pool for medium buffers (small frames)
  buffer_pool_t *large_pool;  // Pool for large buffers (large frames)
  buffer_pool_t *xlarge_pool; // Pool for extra large buffers (1MB+ frames)
  pthread_mutex_t pool_mutex; // Protect all pools
  // Global statistics
  uint64_t total_allocs;     // Total allocation requests
  uint64_t pool_hits;        // Allocations satisfied from pools
  uint64_t malloc_fallbacks; // Had to use malloc
} data_buffer_pool_t;

// Buffer pool management functions
data_buffer_pool_t *data_buffer_pool_create(void);
void data_buffer_pool_destroy(data_buffer_pool_t *pool);
void *data_buffer_pool_alloc(data_buffer_pool_t *pool, size_t size);
void data_buffer_pool_free(data_buffer_pool_t *pool, void *data, size_t size);
void data_buffer_pool_get_stats(data_buffer_pool_t *pool, uint64_t *hits, uint64_t *misses);

// Global shared buffer pool (singleton)
// Initialize with data_buffer_pool_init_global(), access with data_buffer_pool_get_global()
void data_buffer_pool_init_global(void);
void data_buffer_pool_cleanup_global(void);
data_buffer_pool_t *data_buffer_pool_get_global(void);

// Convenience functions that use the global pool
void *buffer_pool_alloc(size_t size);
void buffer_pool_free(void *data, size_t size);

// Enhanced statistics functions
typedef struct {
  uint64_t small_hits, small_misses, small_returns, small_peak_used, small_bytes;
  uint64_t medium_hits, medium_misses, medium_returns, medium_peak_used, medium_bytes;
  uint64_t large_hits, large_misses, large_returns, large_peak_used, large_bytes;
  uint64_t xlarge_hits, xlarge_misses, xlarge_returns, xlarge_peak_used, xlarge_bytes;
  uint64_t total_allocations, total_bytes, total_pool_usage_percent;
} buffer_pool_detailed_stats_t;

void data_buffer_pool_get_detailed_stats(data_buffer_pool_t *pool, buffer_pool_detailed_stats_t *stats);
void data_buffer_pool_log_stats(data_buffer_pool_t *pool, const char *pool_name);
void buffer_pool_log_global_stats(void); // Log global pool stats
