/**
 * @file stream.c
 * @brief Multi-Client Video Mixing and Per-Client ASCII Frame Generation
 *
 * This module represents the heart of ASCII-Chat's video processing pipeline,
 * implementing sophisticated multi-client video mixing and personalized ASCII
 * frame generation. It was extracted from the monolithic server.c to provide
 * clean separation between video processing and other server concerns.
 *
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Collect video frames from all active clients
 * 2. Create composite video layouts (single client, 2x2, 3x3 grids)
 * 3. Generate client-specific ASCII art with terminal capability awareness
 * 4. Handle frame caching to prevent flicker during buffer underruns
 * 5. Manage memory efficiently with buffer pools and zero-copy operations
 * 6. Support advanced rendering modes (half-block, color, custom palettes)
 *
 * VIDEO MIXING ARCHITECTURE:
 * ==========================
 * The mixing system operates in several stages:
 *
 * 1. FRAME COLLECTION:
 *    - Scans all active clients for available video frames
 *    - Uses per-client frame buffers (ringbuffers) for burst handling
 *    - Implements aggressive frame dropping to maintain real-time performance
 *    - Caches most recent frame per client to prevent flicker
 *
 * 2. LAYOUT CALCULATION:
 *    - Determines grid size based on number of active video sources
 *    - Supports: single (1x1), side-by-side (2x1), 2x2, 3x2, 3x3 layouts
 *    - Calculates aspect-ratio preserving dimensions for each cell
 *
 * 3. COMPOSITE GENERATION:
 *    - Creates composite image with proper aspect ratio handling
 *    - Places each client's video in appropriate grid cell
 *    - Supports both normal and half-block rendering modes
 *
 * 4. ASCII CONVERSION:
 *    - Converts composite to ASCII using client-specific capabilities
 *    - Respects terminal color support, palette preferences, UTF-8 capability
 *    - Generates ANSI escape sequences for color output
 *
 * 5. PACKET GENERATION:
 *    - Wraps ASCII frames in protocol packets with metadata
 *    - Includes checksums, dimensions, and capability flags
 *    - Queues packets for delivery via client send threads
 *
 * PER-CLIENT CUSTOMIZATION:
 * =========================
 * Unlike traditional video mixing that generates one output, this system
 * creates personalized frames for each client:
 *
 * TERMINAL CAPABILITY AWARENESS:
 * - Color depth: 1-bit (mono), 8-color, 16-color, 256-color, 24-bit RGB
 * - Character support: ASCII-only vs UTF-8 box drawing
 * - Render modes: foreground, background, half-block (2x resolution)
 * - Custom ASCII palettes: brightness-to-character mapping
 *
 * PERFORMANCE OPTIMIZATIONS:
 * - Per-client palette caches (avoid repeated initialization)
 * - Frame caching with intelligent invalidation
 * - Buffer pool allocation to reduce malloc/free overhead
 * - Aggressive frame dropping under load
 * - Zero-copy operations where possible
 *
 * THREADING AND CONCURRENCY:
 * ===========================
 * This module operates in a high-concurrency environment:
 *
 * THREAD SAFETY MECHANISMS:
 * - Per-client cached frame mutexes (prevents corruption)
 * - Reader-writer locks on client manager (allows concurrent reads)
 * - Buffer pool thread safety (lock-free where possible)
 * - Atomic snapshot operations for client state
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Supports 60fps per client with linear scaling
 * - Handles burst traffic with frame buffer overruns
 * - Minimal CPU overhead in common case (cached frames)
 * - Memory usage scales with number of active clients
 *
 * FRAME BUFFER MANAGEMENT:
 * ========================
 *
 * CACHING STRATEGY:
 * Each client maintains a cached copy of their most recent frame:
 * - Prevents flicker when new frames aren't available
 * - Allows continued rendering during network issues
 * - Automatically updated when new frames arrive
 * - Memory managed via buffer pools
 *
 * BUFFER OVERFLOW HANDLING:
 * When clients send frames faster than processing:
 * - Aggressive frame dropping (keep only latest)
 * - Logarithmic drop rate based on buffer occupancy
 * - Maintains real-time performance under load
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - render.c: Calls frame generation functions at 60fps per client
 * - client.c: Provides client state and capabilities
 * - protocol.c: Stores incoming video frames in per-client buffers
 * - image2ascii/: Performs actual RGB-to-ASCII conversion
 * - palette.c: Manages client-specific character palettes
 *
 * WHY THIS MODULAR DESIGN:
 * =========================
 * The original server.c contained all video mixing logic inline, making it:
 * - Impossible to understand the video pipeline
 * - Difficult to optimize performance bottlenecks
 * - Hard to add new rendering features
 * - Challenging to debug frame generation issues
 *
 * This separation provides:
 * - Clear video processing pipeline
 * - Isolated performance optimization
 * - Easier feature development
 * - Better error isolation and debugging
 *
 * MEMORY MANAGEMENT PHILOSOPHY:
 * ==============================
 * - All allocations use buffer pools where possible
 * - Frame data is reference-counted (avoid copies)
 * - Cached frames use deep copies (prevent corruption)
 * - Automatic cleanup on client disconnect
 * - Graceful degradation on allocation failures
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 * @see render.c For frame generation scheduling
 * @see image2ascii/ For RGB-to-ASCII conversion implementation
 * @see palette.c For client palette management
 */

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "stream.h"
#include "client.h"
#include "common.h"
#include "buffer_pool.h"
#include "network.h"
#include "packet_queue.h"
#include "ringbuffer.h"
#include "image2ascii/image.h"
#include "image2ascii/ascii.h"
#include "aspect_ratio.h"
#include "crc32_hw.h"

/**
 * @brief Global frame cache mutex for legacy frame caching system
 *
 * Protects access to the legacy global frame cache during the transition
 * period to per-client frame caching. This will be removed once all code
 * paths use per-client caching exclusively.
 *
 * @deprecated Use per-client cached_frame_mutex instead
 */
mutex_t g_frame_cache_mutex = {0};

/**
 * @brief Global frame cache entry for legacy system
 *
 * Contains cached frame data from the original single-output video system.
 * This is being phased out in favor of per-client frame caching.
 *
 * @deprecated Use client_info_t.last_valid_frame instead
 */
frame_cache_entry_t g_frame_cache = {0};

/**
 * @brief Legacy global frame cache variables (being phased out)
 *
 * These variables supported the original single-output video system where
 * all clients received the same ASCII frame. The new per-client system
 * generates customized frames for each client's terminal capabilities.
 *
 * MIGRATION STATUS:
 * - g_last_valid_frame: Replaced by client->last_valid_frame.data
 * - g_last_valid_frame_size: Replaced by client->last_valid_frame.size
 * - g_last_frame_width/height: Replaced by client capabilities
 * - g_last_frame_was_color: Replaced by per-client color detection
 *
 * @deprecated Use per-client frame caching in client_info_t
 */
static char *g_last_valid_frame = NULL;           ///< Legacy cached frame data
static size_t g_last_valid_frame_size = 0;        ///< Legacy cached frame size
static unsigned short g_last_frame_width = 0;     ///< Legacy frame width
static unsigned short g_last_frame_height = 0;    ///< Legacy frame height
static bool g_last_frame_was_color = false;       ///< Legacy color flag

/* ============================================================================
 * Legacy Frame Cache Management
 * ============================================================================
 */

/**
 * @brief Clean up legacy global frame cache on server shutdown
 *
 * Frees the legacy global frame cache to prevent memory leaks during server
 * shutdown. This function will be removed once the transition to per-client
 * frame caching is complete.
 *
 * CLEANUP OPERATIONS:
 * - Frees cached frame data if allocated
 * - Resets all cache state variables to prevent stale data
 * - Thread-safe operation using global cache mutex
 *
 * THREAD SAFETY:
 * - Acquires g_frame_cache_mutex for atomic cleanup
 * - Safe to call multiple times (idempotent)
 * - Can be called concurrently with frame generation
 *
 * @deprecated This function supports legacy code paths only
 * @note Per-client frame caches are cleaned up in remove_client()
 */

void cleanup_frame_cache() {
  mutex_lock(&g_frame_cache_mutex);
  if (g_last_valid_frame) {
    free(g_last_valid_frame);
    g_last_valid_frame = NULL;
    g_last_valid_frame_size = 0;
    // Reset other cache state to prevent stale data
    g_last_frame_width = 0;
    g_last_frame_height = 0;
    g_last_frame_was_color = false;
  }
  mutex_unlock(&g_frame_cache_mutex);
}

/* ============================================================================
 * Client Lookup Utilities
 * ============================================================================
 */

/**
 * @brief Fast O(1) client lookup using hash table optimization
 *
 * Provides high-performance client lookup for frame generation operations.
 * This is called frequently during video processing, so optimization is
 * critical for maintaining real-time performance.
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Time complexity: O(1) average case (hash table lookup)
 * - Space complexity: O(1)
 * - No locking required (hash table provides thread safety)
 *
 * USAGE IN VIDEO PIPELINE:
 * - Called once per client per frame generation cycle
 * - Used to access client capabilities and preferences
 * - Enables client-specific rendering optimizations
 *
 * @param client_id Unique identifier for target client
 * @return Pointer to client_info_t if found, NULL if not found or invalid
 *
 * @note This function provides faster access than find_client_by_id()
 * @note Returns direct pointer - caller should use snapshot pattern for safety
 * @see hashtable_lookup() For underlying hash table implementation
 */
static client_info_t *find_client_by_id_fast(uint32_t client_id) {
  if (g_client_manager.client_hashtable) {
    return hashtable_lookup(g_client_manager.client_hashtable, client_id);
  }
  return NULL;
}

/* ============================================================================
 * Per-Client Video Mixing and Frame Generation
 * ============================================================================
 */


/**
 * @brief Generate personalized ASCII frame for a specific client
 *
 * This is the core video mixing function that creates customized ASCII art
 * frames for individual clients. It collects video from all active clients,
 * creates an appropriate grid layout, and converts to ASCII using the target
 * client's terminal capabilities.
 *
 * ALGORITHM OVERVIEW:
 * ===================
 *
 * 1. FRAME COLLECTION PHASE:
 *    - Scan all active clients for available video frames
 *    - Use ringbuffer occupancy to determine frame reading strategy
 *    - Implement aggressive frame dropping under load (maintains real-time)
 *    - Cache most recent frame per client to prevent flicker
 *
 * 2. LAYOUT CALCULATION PHASE:
 *    - Determine grid dimensions based on active client count
 *    - Calculate cell sizes with aspect ratio preservation
 *    - Handle special cases: single client, multiple clients
 *
 * 3. COMPOSITE GENERATION PHASE:
 *    - Create composite image with appropriate dimensions
 *    - Place each client's video in grid cell with proper scaling
 *    - Support both normal and half-block rendering modes
 *
 * 4. ASCII CONVERSION PHASE:
 *    - Convert composite to ASCII using client capabilities
 *    - Apply client-specific palette and color settings
 *    - Generate ANSI escape sequences if supported
 *
 * PERFORMANCE OPTIMIZATIONS:
 * ==========================
 *
 * FRAME BUFFER MANAGEMENT:
 * - Aggressive frame dropping: >30% occupancy = read latest only
 * - Per-client frame caching: prevents flicker during underruns
 * - Buffer pool allocation: reduces malloc/free overhead
 * - Zero-copy operations where possible
 *
 * CONCURRENCY OPTIMIZATIONS:
 * - Reader locks on client manager (allows parallel execution)
 * - Per-client cache mutexes (fine-grained locking)
 * - Atomic snapshots of client state
 * - Thread-safe buffer operations
 *
 * CLIENT CAPABILITY INTEGRATION:
 * ===============================
 *
 * TERMINAL AWARENESS:
 * - Respects client's color depth capabilities
 * - Uses client's preferred ASCII palette
 * - Handles UTF-8 vs ASCII-only terminals
 * - Supports half-block mode for 2x vertical resolution
 *
 * RENDERING MODES SUPPORTED:
 * - RENDER_MODE_FOREGROUND: Traditional ASCII art
 * - RENDER_MODE_BACKGROUND: Inverted colors for better contrast
 * - RENDER_MODE_HALF_BLOCK: Unicode half-blocks for 2x resolution
 *
 * ERROR HANDLING STRATEGY:
 * ========================
 * - Invalid dimensions: Logged and frame generation aborted
 * - Memory allocation failures: Graceful degradation, cleanup resources
 * - Client disconnections: Skip client in current frame, continue
 * - Buffer overruns: Frame dropping maintains real-time performance
 * - Capability missing: Wait for capabilities before generating frames
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - All image operations use buffer pool allocation
 * - Frame data is properly freed after use
 * - Cached frames use deep copies (prevent corruption)
 * - Automatic cleanup on function exit
 *
 * @param target_client_id Client who will receive this customized frame
 * @param width Terminal width in characters for this client
 * @param height Terminal height in characters for this client
 * @param wants_stretch Unused parameter (aspect ratio always preserved)
 * @param out_size Pointer to store resulting ASCII frame size
 *
 * @return Allocated ASCII frame string (caller must free), or NULL on error
 *
 * @note This function is called at 60fps per client by render threads
 * @note Generated frame is customized for target client's capabilities
 * @note Memory allocated from buffer pool - caller must free properly
 *
 * @warning Function performs extensive validation - invalid input returns NULL
 * @warning Client capabilities must be received before frame generation
 *
 * @see ascii_convert_with_capabilities() For ASCII conversion implementation
 * @see framebuffer_read_multi_frame() For frame buffer operations
 * @see calculate_fit_dimensions_pixel() For aspect ratio calculations
 */
char *create_mixed_ascii_frame_for_client(uint32_t target_client_id, unsigned short width, unsigned short height,
                                          bool wants_stretch, size_t *out_size) {
  (void)wants_stretch; // Unused - we always handle aspect ratio ourselves
  if (!out_size || width == 0 || height == 0) {
    log_error("Invalid parameters for create_mixed_ascii_frame_for_client: width=%u, height=%u, out_size=%p", width,
              height, out_size);
    return NULL;
  }

  // Collect all active clients and their image sources
  typedef struct {
    image_t *image;
    uint32_t client_id;
    bool has_video; // Whether this client has video or is just a placeholder
  } image_source_t;

  image_source_t sources[MAX_CLIENTS];
  int source_count = 0; // This will be ALL active clients, not just those with video

  // CONCURRENCY FIX: Now using READ lock since framebuffer operations are thread-safe
  // framebuffer_read_multi_frame() now uses internal mutex for thread safety
  // Multiple video generation threads can safely access client list concurrently
  rwlock_rdlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

    // Include ALL active clients in the grid, not just those sending video
    if (client->active && source_count < MAX_CLIENTS) {
      sources[source_count].client_id = client->client_id;
      sources[source_count].image = NULL; // Will be set if video is available
      sources[source_count].has_video = false;

      // Only try to get video from clients that are actually sending it
      if (client->is_sending_video) {
        // Skip clients who haven't sent their first real frame yet to avoid blank frames
        if (!client->has_cached_frame && client->incoming_video_buffer) {
          size_t occupancy = ringbuffer_size(client->incoming_video_buffer->rb);
          if (occupancy == 0) {
            // No frames available and no cached frame - client gets placeholder slot
            source_count++;
            continue;
          }
        }

        // ENHANCED RINGBUFFER USAGE: Dynamic frame reading based on buffer occupancy
        // Same logic as global version but with per-client optimizations
        multi_source_frame_t current_frame = {0};
        bool got_new_frame = false;
        int frames_to_read = 1;

        // Check buffer occupancy to determine reading strategy
        if (client->incoming_video_buffer) {
          size_t occupancy = ringbuffer_size(client->incoming_video_buffer->rb);
          size_t capacity = client->incoming_video_buffer->rb->capacity;
          double occupancy_ratio = (double)occupancy / (double)capacity;

          if (occupancy_ratio > 0.3) {
            // AGGRESSIVE: Skip to latest frame for ANY significant occupancy
            frames_to_read = (int)occupancy - 1; // Read all but keep latest
            if (frames_to_read > 20)
              frames_to_read = 20; // Cap to avoid excessive processing
            if (frames_to_read < 1)
              frames_to_read = 1; // Always read at least 1
          }

          // Read frames (potentially multiple to drain buffer)
          multi_source_frame_t discard_frame = {0};
          for (int read_count = 0; read_count < frames_to_read; read_count++) {
            bool frame_available = framebuffer_read_multi_frame(
                client->incoming_video_buffer, (read_count == frames_to_read - 1) ? &current_frame : &discard_frame);
            if (!frame_available) {
              break; // No more frames available
            }

            if (read_count == frames_to_read - 1) {
              // This is the frame we'll use
              got_new_frame = true;
            } else {
              // This is a frame we're discarding to catch up
              if (discard_frame.data) {
                buffer_pool_free(discard_frame.data, discard_frame.size);
                discard_frame.data = NULL;
              }
            }
          }

          if (got_new_frame) {
            // CONCURRENCY FIX: Lock only THIS client's cached frame data
            mutex_lock(&client->cached_frame_mutex);

            // Got a new frame - update our cache
            // Free old cached frame data if we had one
            if (client->has_cached_frame && client->last_valid_frame.data) {
              buffer_pool_free(client->last_valid_frame.data, client->last_valid_frame.size);
            }

            // Cache this frame for future use (make a copy)
            client->last_valid_frame.magic = current_frame.magic;
            client->last_valid_frame.source_client_id = current_frame.source_client_id;
            client->last_valid_frame.frame_sequence = current_frame.frame_sequence;
            client->last_valid_frame.timestamp = current_frame.timestamp;
            client->last_valid_frame.size = current_frame.size;

            // Allocate and copy frame data for cache using buffer pool
            client->last_valid_frame.data = buffer_pool_alloc(current_frame.size);
            if (client->last_valid_frame.data) {
              memcpy(client->last_valid_frame.data, current_frame.data, current_frame.size);
              client->has_cached_frame = true;
            } else {
              log_error("Failed to allocate cache buffer for client %u", client->client_id);
              client->has_cached_frame = false;
            }

            mutex_unlock(&client->cached_frame_mutex);
          }
        }

        // Use either the new frame or the cached frame
        multi_source_frame_t *frame_to_use = NULL;
        bool using_cached_frame = false;
        if (got_new_frame) {
          frame_to_use = &current_frame;
        } else {
          // CONCURRENCY FIX: Lock THIS client's cached frame for reading
          mutex_lock(&client->cached_frame_mutex);
          if (client->has_cached_frame) {
            frame_to_use = &client->last_valid_frame;
            using_cached_frame = true;
          } else {
            // No cached frame - unlock immediately since we won't use it
            mutex_unlock(&client->cached_frame_mutex);
          }
          // Note: If using_cached_frame=true, we keep the lock held while using the data
        }

        if (frame_to_use && frame_to_use->data && frame_to_use->size > sizeof(uint32_t) * 2) {
          // Parse the image data
          // Format: [width:4][height:4][rgb_data:w*h*3]
          uint32_t img_width = ntohl(*(uint32_t *)frame_to_use->data);
          uint32_t img_height = ntohl(*(uint32_t *)(frame_to_use->data + sizeof(uint32_t)));

          // Validate dimensions are reasonable (max 4K resolution)
          if (img_width == 0 || img_width > 4096 || img_height == 0 || img_height > 4096) {
            log_error("Per-client: Invalid image dimensions from client %u: %ux%u (data may be corrupted)",
                      client->client_id, img_width, img_height);
            // Don't free cached frame, just skip this client
            if (got_new_frame) {
              buffer_pool_free(current_frame.data, current_frame.size);
            }
            // Unlock cached frame mutex if we were using cached data
            if (using_cached_frame) {
              mutex_unlock(&client->cached_frame_mutex);
            }
            source_count++;
            continue;
          }

          // Validate that the frame size matches expected size
          size_t expected_size = sizeof(uint32_t) * 2 + (size_t)img_width * (size_t)img_height * sizeof(rgb_t);
          if (frame_to_use->size != expected_size) {
            log_error("Per-client: Frame size mismatch from client %u: got %zu, expected %zu for %ux%u image",
                      client->client_id, frame_to_use->size, expected_size, img_width, img_height);
            // Don't free cached frame, just skip this client
            if (got_new_frame) {
              buffer_pool_free(current_frame.data, current_frame.size);
            }
            // Unlock cached frame mutex if we were using cached data
            if (using_cached_frame) {
              mutex_unlock(&client->cached_frame_mutex);
            }
            source_count++;
            continue;
          }

          // Extract pixel data
          rgb_t *pixels = (rgb_t *)(frame_to_use->data + sizeof(uint32_t) * 2);

          // Create image from buffer pool for consistent video pipeline management
          image_t *img = image_new_from_pool(img_width, img_height);
          if (img) {
            memcpy(img->pixels, pixels, (size_t)img_width * (size_t)img_height * sizeof(rgb_t));
            sources[source_count].image = img;
            sources[source_count].has_video = true;
          }

          // Free the current frame data if we got a new one (cached frame persists)
          // The framebuffer allocates this data via buffer_pool_alloc() and we must free it
          if (got_new_frame && current_frame.data) {
            buffer_pool_free(current_frame.data, current_frame.size);
            current_frame.data = NULL; // Prevent double-free
          }
        }

        // CONCURRENCY FIX: Unlock cached frame mutex if we were using cached data
        if (using_cached_frame) {
          mutex_unlock(&client->cached_frame_mutex);
        }
      } // End of if (client->is_sending_video)

      // Increment source count for this active client (with or without video)
      source_count++;
    }
  }
  rwlock_unlock(&g_client_manager_rwlock);

  // No active video sources - don't generate placeholder frames
  if (source_count == 0) {
    log_debug("Per-client %u: No video sources available - returning NULL frame", target_client_id);
    *out_size = 0;
    return NULL;
  }

  // Create composite image for multiple sources with grid layout (same logic as global version)
  image_t *composite = NULL;

  if (source_count == 1 && sources[0].image) {
    // Single source - check if target client wants half-block mode for 2x resolution
    client_info_t *target_client = find_client_by_id_fast(target_client_id);
    bool use_half_block = target_client && target_client->has_terminal_caps &&
                          target_client->terminal_caps.render_mode == RENDER_MODE_HALF_BLOCK;

    int composite_width_px, composite_height_px;

    if (use_half_block) {
      // Half-block mode: use full terminal dimensions for 2x resolution
      composite_width_px = width;
      composite_height_px = height * 2;
    } else {
      // Normal modes: use aspect-ratio fitted dimensions
      calculate_fit_dimensions_pixel(sources[0].image->w, sources[0].image->h, width, height, &composite_width_px,
                                     &composite_height_px);
    }

    // Create composite from buffer pool for consistent memory management
    composite = image_new_from_pool(composite_width_px, composite_height_px);
    if (!composite) {
      log_error("Per-client %u: Failed to create composite image", target_client_id);
      *out_size = 0;
      for (int i = 0; i < source_count; i++) {
        image_destroy_to_pool(sources[i].image);
      }
      return NULL;
    }

    image_clear(composite);

    if (use_half_block) {
      // Half-block mode: manual aspect ratio and centering to preserve 2x resolution
      float src_aspect = (float)sources[0].image->w / (float)sources[0].image->h;
      float target_aspect = (float)composite_width_px / (float)composite_height_px;

      int fitted_width, fitted_height;
      if (src_aspect > target_aspect) {
        // Source is wider - fit to width
        fitted_width = composite_width_px;
        fitted_height = (int)(composite_width_px / src_aspect);
      } else {
        // Source is taller - fit to height
        fitted_height = composite_height_px;
        fitted_width = (int)(composite_height_px * src_aspect);
      }

      // Calculate centering offsets
      int x_offset = (composite_width_px - fitted_width) / 2;
      int y_offset = (composite_height_px - fitted_height) / 2;

      // Create fitted image from buffer pool
      image_t *fitted = image_new_from_pool(fitted_width, fitted_height);
      if (fitted) {
        image_resize(sources[0].image, fitted);

        // Copy fitted image to center of composite
        for (int y = 0; y < fitted_height; y++) {
          for (int x = 0; x < fitted_width; x++) {
            int src_idx = y * fitted_width + x;
            int dst_x = x_offset + x;
            int dst_y = y_offset + y;
            int dst_idx = dst_y * composite_width_px + dst_x;

            if (dst_x >= 0 && dst_x < composite_width_px && dst_y >= 0 && dst_y < composite_height_px) {
              composite->pixels[dst_idx] = fitted->pixels[src_idx];
            }
          }
        }
        image_destroy_to_pool(fitted);
      }
    } else {
      // Normal modes: simple resize to fit calculated dimensions
      image_resize(sources[0].image, composite);
    }
  } else if (source_count > 1) {
    // Multiple sources - create grid layout
    client_info_t *target_client = find_client_by_id_fast(target_client_id);
    bool use_half_block_multi = target_client && target_client->has_terminal_caps &&
                                target_client->terminal_caps.render_mode == RENDER_MODE_HALF_BLOCK;

    int composite_width_px = width;
    int composite_height_px = use_half_block_multi ? height * 2 : height;

    composite = image_new_from_pool(composite_width_px, composite_height_px);
    if (!composite) {
      log_error("Per-client %u: Failed to create composite image", target_client_id);
      *out_size = 0;
      for (int i = 0; i < source_count; i++) {
        image_destroy_to_pool(sources[i].image);
      }
      return NULL;
    }

    image_clear(composite);

    // Calculate grid dimensions based on source count
    int grid_cols = (source_count == 2) ? 2 : (source_count <= 4) ? 2 : 3;
    int grid_rows = (source_count + grid_cols - 1) / grid_cols;

    // Calculate cell dimensions in characters (for layout)
    int cell_width_chars = width / grid_cols;
    int cell_height_chars = height / grid_rows;

    // Convert cell dimensions to pixels for image operations
    int cell_width_px = cell_width_chars;       // 1:1 mapping
    int cell_height_px = cell_height_chars * 2; // 2:1 mapping to match composite dimensions

    // Place each source in the grid
    for (int i = 0; i < source_count && i < 9; i++) { // Max 9 sources in 3x3 grid
      if (!sources[i].image)
        continue;

      int row = i / grid_cols;
      int col = i % grid_cols;

      // Calculate cell position in pixels
      int cell_x_offset_px = col * cell_width_px;
      int cell_y_offset_px = row * cell_height_px;

      float src_aspect = (float)sources[i].image->w / (float)sources[i].image->h;
      float cell_aspect = (float)cell_width_px / (float)cell_height_px;

      int target_width_px, target_height_px;
      if (src_aspect > cell_aspect) {
        // Image is wider than cell - fit to width
        target_width_px = cell_width_px;
        target_height_px = (int)(cell_width_px / src_aspect + 0.5f);
      } else {
        // Image is taller than cell - fit to height
        target_height_px = cell_height_px;
        target_width_px = (int)(cell_height_px * src_aspect + 0.5f);
      }

      // Ensure target dimensions don't exceed cell dimensions
      if (target_width_px > cell_width_px)
        target_width_px = cell_width_px;
      if (target_height_px > cell_height_px)
        target_height_px = cell_height_px;

      // Create resized image with standard allocation
      image_t *resized = image_new_from_pool(target_width_px, target_height_px);
      if (resized) {
        image_resize(sources[i].image, resized);

        // Center the resized image within the cell (pixel coordinates)
        int x_padding_px = (cell_width_px - target_width_px) / 2;
        int y_padding_px = (cell_height_px - target_height_px) / 2;

        // Copy resized image to composite with proper bounds checking
        for (int y = 0; y < target_height_px; y++) {
          for (int x = 0; x < target_width_px; x++) {
            int src_idx = y * target_width_px + x;
            int dst_x = cell_x_offset_px + x_padding_px + x;
            int dst_y = cell_y_offset_px + y_padding_px + y;

            // CRITICAL FIX: Use correct stride for composite image
            int dst_idx = dst_y * composite_width_px + dst_x;

            // Bounds checking with correct composite dimensions
            if (src_idx >= 0 && src_idx < resized->w * resized->h && dst_idx >= 0 &&
                dst_idx < composite->w * composite->h && dst_x >= 0 && dst_x < composite_width_px && dst_y >= 0 &&
                dst_y < composite_height_px) {
              composite->pixels[dst_idx] = resized->pixels[src_idx];
            }
          }
        }

        image_destroy_to_pool(resized);
      }
    }
  } else {
    // No sources, create empty composite with standard allocation
    composite = image_new_from_pool(width, height * 2);
    if (!composite) {
      log_error("Per-client %u: Failed to create empty image", target_client_id);
      *out_size = 0;
      return NULL;
    }
    image_clear(composite);
  }

  // Find the target client to get their terminal capabilities
  client_info_t *target_client = find_client_by_id_fast(target_client_id);
  char *ascii_frame = NULL;

  if (target_client && target_client->has_terminal_caps) {
    // Use capability-aware ASCII conversion for better terminal compatibility
    mutex_lock(&target_client->client_state_mutex);
    terminal_capabilities_t caps_snapshot = target_client->terminal_caps;
    mutex_unlock(&target_client->client_state_mutex);

    if (target_client->client_palette_initialized) {
      // Render with client's custom palette using enhanced capabilities
      if (caps_snapshot.render_mode == RENDER_MODE_HALF_BLOCK) {
        ascii_frame = ascii_convert_with_capabilities(composite, width, height * 2, &caps_snapshot, true, false,
                                                      target_client->client_palette_chars,
                                                      target_client->client_luminance_palette);
      } else {
        ascii_frame = ascii_convert_with_capabilities(composite, width, height, &caps_snapshot, true, false,
                                                      target_client->client_palette_chars,
                                                      target_client->client_luminance_palette);
      }
    } else {
      // Client palette not initialized - this is an error condition
      log_error("Client %u palette not initialized - cannot render frame", target_client_id);
      ascii_frame = NULL;
    }
  } else {
    // Don't send frames until we receive client capabilities - saves bandwidth and CPU
    log_debug("Per-client %u: Waiting for terminal capabilities before sending frames (no capabilities received yet)",
              target_client_id);
    ascii_frame = NULL;
  }

  if (ascii_frame) {
    *out_size = strlen(ascii_frame);
  } else {
    log_error("Per-client %u: Failed to convert image to ASCII", target_client_id);
    *out_size = 0;
  }

  // Clean up using standard deallocation
  image_destroy_to_pool(composite);
  for (int i = 0; i < source_count; i++) {
    if (sources[i].image) {
      image_destroy_to_pool(sources[i].image);
    }
  }

  return ascii_frame;
}


/* ============================================================================
 * Frame Queuing and Delivery Functions
 * ============================================================================
 */

/**
 * @brief Queue ASCII frame for delivery to specific client
 *
 * Packages a generated ASCII frame into a protocol packet and queues it
 * for asynchronous delivery to the target client. This function handles
 * all protocol details including headers, checksums, and metadata.
 *
 * PACKET STRUCTURE CREATED:
 * =========================
 * - ascii_frame_packet_t header containing:
 *   - width, height: Client terminal dimensions
 *   - original_size: Uncompressed frame size
 *   - compressed_size: Compressed size (currently 0)
 *   - checksum: CRC32 of frame data for integrity
 *   - flags: Capability flags (color support, etc.)
 * - Frame data: Complete ASCII art string with ANSI codes
 *
 * PROTOCOL INTEGRATION:
 * =====================
 * - Converts dimensions to network byte order
 * - Generates CRC32 checksum for error detection
 * - Sets appropriate capability flags based on client
 * - Uses PACKET_TYPE_ASCII_FRAME packet type
 *
 * DELIVERY MECHANISM:
 * ===================
 * - Queues packet in client's video packet queue
 * - Send thread delivers packet asynchronously
 * - Queue overflow handling prevents memory exhaustion
 * - Error conditions are logged but don't affect other clients
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - Creates temporary packet buffer for header+data combination
 * - packet_queue_enqueue() copies data (safe to free temp buffer)
 * - No memory leaks on success or failure paths
 * - Buffer pool allocation not used here (temporary buffer)
 *
 * ERROR HANDLING:
 * ===============
 * - Invalid parameters: Return -1, no side effects
 * - Memory allocation failure: Logged, return -1
 * - Queue overflow: Logged as debug (expected under load)
 * - Client shutdown: Graceful failure without error spam
 *
 * @param client Target client for frame delivery
 * @param ascii_frame Generated ASCII art string (null-terminated)
 * @param frame_size Size of ASCII frame in bytes
 *
 * @return 0 on successful queuing, -1 on error
 *
 * @note Frame delivery happens asynchronously via send thread
 * @note Queue overflow drops frames to maintain real-time performance
 * @note Checksums enable client-side integrity verification
 *
 * @see packet_queue_enqueue() For underlying queue implementation
 * @see asciichat_crc32() For checksum generation
 */
int queue_ascii_frame_for_client(client_info_t *client, const char *ascii_frame, size_t frame_size) {
  if (!client || !client->video_queue || !ascii_frame || frame_size == 0) {
    return -1;
  }

  // Create ASCII frame packet header with metadata
  ascii_frame_packet_t frame_header = {
      .width = htonl(client->width),
      .height = htonl(client->height),
      .original_size = htonl((uint32_t)frame_size),
      .compressed_size = htonl(0), // Not using compression for per-client frames yet
      .checksum = htonl(asciichat_crc32(ascii_frame, frame_size)),
      .flags = htonl((client->terminal_caps.color_level > TERM_COLOR_NONE) ? FRAME_FLAG_HAS_COLOR : 0)};

  // Allocate buffer for complete packet (header + data)
  size_t packet_size = sizeof(ascii_frame_packet_t) + frame_size;
  char *packet_buffer = malloc(packet_size);
  if (!packet_buffer) {
    log_error("Failed to allocate packet buffer for client %u", client->client_id);
    return -1;
  }

  // Copy header and frame data into single buffer
  memcpy(packet_buffer, &frame_header, sizeof(ascii_frame_packet_t));
  memcpy(packet_buffer + sizeof(ascii_frame_packet_t), ascii_frame, frame_size);

  // Queue the complete frame as a single packet
  int result = packet_queue_enqueue(client->video_queue, PACKET_TYPE_ASCII_FRAME, packet_buffer, packet_size, 0, true);

  // Free the temporary packet buffer (packet_queue_enqueue copies data when copy=true)
  free(packet_buffer);

  if (result < 0) {
    log_debug("Failed to queue ASCII frame for client %u: queue full or shutdown", client->client_id);
    return -1;
  }

  return 0;
}

/**
 * @brief Queue audio data for delivery to specific client
 *
 * Queues mixed audio data for delivery to a specific client. This is a
 * simple wrapper around the packet queue system for audio delivery.
 *
 * AUDIO PIPELINE INTEGRATION:
 * ============================
 * - Called by audio mixing threads after combining multiple client streams
 * - Audio data is already in final format (float samples, mixed)
 * - No additional processing required at this stage
 *
 * PACKET DETAILS:
 * ===============
 * - Uses PACKET_TYPE_AUDIO packet type
 * - Audio data is raw float samples
 * - No special headers or metadata required
 * - Sample rate and format determined by audio system
 *
 * DELIVERY CHARACTERISTICS:
 * =========================
 * - Higher priority than video packets (lower latency)
 * - Uses client's audio packet queue
 * - Send thread prioritizes audio over video
 * - Queue overflow drops oldest audio to maintain real-time
 *
 * ERROR HANDLING:
 * ===============
 * - Invalid parameters: Return -1 immediately
 * - Queue overflow: Handled by packet queue (drops old data)
 * - Client shutdown: Graceful failure
 *
 * @param client Target client for audio delivery
 * @param audio_data Mixed audio samples (float format)
 * @param data_size Size of audio data in bytes
 *
 * @return 0 on successful queuing, -1 on error
 *
 * @note Audio has priority over video in send thread
 * @note Real-time audio requires queue overflow handling
 * @see packet_queue_enqueue() For underlying queue implementation
 * @see mixer.c For audio mixing implementation
 */
int queue_audio_for_client(client_info_t *client, const void *audio_data, size_t data_size) {
  if (!client || !client->audio_queue || !audio_data || data_size == 0) {
    return -1;
  }

  return packet_queue_enqueue(client->audio_queue, PACKET_TYPE_AUDIO, audio_data, data_size, 0, true);
}
