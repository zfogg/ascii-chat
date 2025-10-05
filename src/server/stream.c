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
 * 4. Process latest frames from double-buffer system for real-time performance
 * 5. Manage memory efficiently with buffer pools and zero-copy operations
 * 6. Support advanced rendering modes (half-block, color, custom palettes)
 *
 * VIDEO MIXING ARCHITECTURE:
 * ==========================
 * The mixing system operates in several stages:
 *
 * 1. FRAME COLLECTION:
 *    - Scans all active clients for available video frames
 *    - Uses per-client double-buffer system for smooth frame handling
 *    - Implements aggressive frame dropping to maintain real-time performance
 *    - Always uses the latest available frame for professional-grade performance
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
 * - Double-buffer system for smooth frame delivery
 * - Buffer pool allocation to reduce malloc/free overhead
 * - Aggressive frame dropping under load
 * - Zero-copy operations where possible
 *
 * THREADING AND CONCURRENCY:
 * ===========================
 * This module operates in a high-concurrency environment:
 *
 * THREAD SAFETY MECHANISMS:
 * - Double-buffer thread safety (atomic operations)
 * - Reader-writer locks on client manager (allows concurrent reads)
 * - Buffer pool thread safety (lock-free where possible)
 * - Atomic snapshot operations for client state
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Supports 60fps per client with linear scaling
 * - Handles burst traffic with frame buffer overruns
 * - Minimal CPU overhead with double-buffer system
 * - Memory usage scales with number of active clients
 *
 * FRAME BUFFER MANAGEMENT:
 * ========================
 *
 * DOUBLE-BUFFER STRATEGY:
 * Each client uses a double-buffer system for smooth frame delivery:
 * - Always provides the latest available frame
 * - No frame caching complexity or stale data concerns
 * - Professional-grade real-time performance like Zoom/Google Meet
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
 * - Double-buffer system eliminates need for frame copies
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
#include <math.h>

#include "stream.h"
#include "client.h"
#include "protocol.h"
#include "common.h"
#include "buffer_pool.h"
#include "network.h"
#include "packet_queue.h"
#include "ringbuffer.h"
#include "video_frame.h"
#include "image2ascii/image.h"
#include "image2ascii/ascii.h"
#include "aspect_ratio.h"
#include "crc32_hw.h"

// Global client manager from client.c - needed for any_clients_sending_video()
extern rwlock_t g_client_manager_rwlock;
extern client_manager_t g_client_manager;

// Track previous active video source count for grid layout change detection
static atomic_int g_previous_active_video_count = 0;

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
 *    - Use double-buffer system to get latest frames
 *    - Implement aggressive frame dropping under load (maintains real-time)
 *    - Always use the most current frame for professional-grade quality
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
 * - Aggressive frame dropping: always read latest available
 * - Double-buffer system: ensures smooth frame delivery
 * - Buffer pool allocation: reduces malloc/free overhead
 * - Zero-copy operations where possible
 *
 * CONCURRENCY OPTIMIZATIONS:
 * - Reader locks on client manager (allows parallel execution)
 * - Double-buffer atomic operations (lock-free access)
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
                                          bool wants_stretch, size_t *out_size, bool *out_grid_changed) {
  (void)wants_stretch; // Unused - we always handle aspect ratio ourselves

  // Initialize output parameter
  if (out_grid_changed) {
    *out_grid_changed = false;
  }

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

  // Check for shutdown before acquiring locks to prevent lock corruption
  if (atomic_load(&g_should_exit)) {
    log_debug("create_mixed_ascii_frame_for_client: shutdown detected, aborting frame generation");
    return NULL;
  }

  // CONCURRENCY FIX: Now using READ lock since framebuffer operations are thread-safe
  // framebuffer_read_multi_frame() now uses internal mutex for thread safety
  // Multiple video generation threads can safely access client list concurrently
  rwlock_rdlock(&g_client_manager_rwlock);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

    // Skip empty client slots to avoid mutex contention
    // FIXED: Only access mutex for initialized clients to avoid accessing uninitialized mutex
    if (atomic_load(&client->client_id) == 0) {
      continue; // Skip uninitialized clients
    }

    // DEADLOCK FIX: Use snapshot pattern to avoid holding both locks simultaneously
    // This prevents deadlock by not acquiring client_state_mutex while holding rwlock
    uint32_t client_id_snapshot = atomic_load(&client->client_id); // Atomic read is safe under rwlock

    // Include ALL active clients in the grid, not just those sending video
    // Thread-safe check for active client - use atomic read to avoid deadlock
    bool is_active = atomic_load(&client->active);
    bool is_sending_video = atomic_load(&client->is_sending_video);

    // DEBUG: Log client state for tracking intermittent bug

    if (is_active && source_count < MAX_CLIENTS) {
      sources[source_count].client_id = client_id_snapshot;
      sources[source_count].image = NULL; // Will be set if video is available
      sources[source_count].has_video = false;

      // Declare these outside the if block so they're accessible later
      multi_source_frame_t current_frame = {0};
      bool got_new_frame = false;

      // Always try to get the last available video frame for consistent ASCII generation
      // The double buffer ensures we always have the last valid frame
      if (is_sending_video) {
        // Use the new double-buffered video frame API
        if (client->incoming_video_buffer) {
          // Get the latest frame (always available from double buffer)
          const video_frame_t *frame = video_frame_get_latest(client->incoming_video_buffer);

          if (frame && frame->data && frame->size > 0) {
            // DEBUG: Log successful frame retrieval
            // We have frame data - copy it to our working structure
            data_buffer_pool_t *pool = data_buffer_pool_get_global();
            if (pool) {
              current_frame.data = data_buffer_pool_alloc(pool, frame->size);
            }
            if (!current_frame.data) {
              current_frame.data = malloc(frame->size);
            }

            if (current_frame.data) {
              memcpy(current_frame.data, frame->data, frame->size);
              current_frame.size = frame->size;
              current_frame.source_client_id = client_id_snapshot;
              current_frame.timestamp = (uint32_t)(frame->capture_timestamp_us / 1000000);
              got_new_frame = true;

              // Don't log frame stats - drops are normal with double buffering
            }
          }
        }
      }

      // Always use the last available frame data for consistent ASCII generation
      // The double buffer ensures we always have the last valid frame
      // We should always try to generate frames from available video data
      // FIX: Always try to process the last available frame, not just when it's "new"
      // The double buffer always provides the last valid frame, so we should always use it
      // Always use the last available frame data for consistent ASCII generation
      // The double buffer ensures we always have the last valid frame
      // We should always try to generate frames from available video data
      // FIX: Always try to process the last available frame, not just when it's "new"
      // The double buffer always provides the last valid frame, so we should always use it
      multi_source_frame_t *frame_to_use = got_new_frame ? &current_frame : NULL;

      // If we have valid frame data, always process it for consistent ASCII generation
      if (frame_to_use && frame_to_use->data && frame_to_use->size > sizeof(uint32_t) * 2) {
        // Parse the image data
        // Format: [width:4][height:4][rgb_data:w*h*3]
        uint32_t img_width = ntohl(*(uint32_t *)frame_to_use->data);
        uint32_t img_height = ntohl(*(uint32_t *)(frame_to_use->data + sizeof(uint32_t)));

        // Debug logging to understand the data
        if (img_width == 0xBEBEBEBE || img_height == 0xBEBEBEBE) {
          log_error("UNINITIALIZED MEMORY DETECTED! First 16 bytes of frame data:");
          uint8_t *bytes = (uint8_t *)frame_to_use->data;
          log_error("  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", bytes[0],
                    bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
                    bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
        }

        // Validate dimensions are reasonable (max 4K resolution)
        if (img_width == 0 || img_width > 4096 || img_height == 0 || img_height > 4096) {
          log_error("Per-client: Invalid image dimensions from client %u: %ux%u (data may be corrupted)",
                    client_id_snapshot, img_width, img_height);
          // Clean up the current frame if we got a new one
          if (got_new_frame) {
            data_buffer_pool_t *pool = data_buffer_pool_get_global();
            if (pool) {
              data_buffer_pool_free(pool, current_frame.data, current_frame.size);
            } else {
              free(current_frame.data);
            }
          }
          source_count++;
          continue;
        }

        // Validate that the frame size matches expected size
        size_t expected_size = sizeof(uint32_t) * 2 + (size_t)img_width * (size_t)img_height * sizeof(rgb_t);
        if (frame_to_use->size != expected_size) {
          log_error("Per-client: Frame size mismatch from client %u: got %zu, expected %zu for %ux%u image",
                    client_id_snapshot, frame_to_use->size, expected_size, img_width, img_height);
          // Clean up the current frame if we got a new one
          if (got_new_frame) {
            data_buffer_pool_t *pool = data_buffer_pool_get_global();
            if (pool) {
              data_buffer_pool_free(pool, current_frame.data, current_frame.size);
            } else {
              free(current_frame.data);
            }
          }
          source_count++;
          continue;
        }

        // Extract pixel data
        rgb_t *pixels = (rgb_t *)(frame_to_use->data + (sizeof(uint32_t) * 2));

        // Create image from buffer pool for consistent video pipeline management
        image_t *img = image_new_from_pool(img_width, img_height);
        if (img) {
          memcpy(img->pixels, pixels, (size_t)img_width * (size_t)img_height * sizeof(rgb_t));
          sources[source_count].image = img;
          sources[source_count].has_video = true;
        }

        // Free the current frame data after processing
        // The double-buffer allocates this data and we must free it
        if (got_new_frame && current_frame.data) {
          data_buffer_pool_t *pool = data_buffer_pool_get_global();
          if (pool) {
            data_buffer_pool_free(pool, current_frame.data, current_frame.size);
          } else {
            free(current_frame.data);
          }
          current_frame.data = NULL; // Prevent double-free
        }
      } // End of if (client->is_sending_video)

      // Increment source count for this active client (with or without video)
      source_count++;
    }
  }
  rwlock_rdunlock(&g_client_manager_rwlock);

  // Count sources that actually have video data
  int sources_with_video = 0;
  for (int i = 0; i < source_count; i++) {
    if (sources[i].has_video && sources[i].image) {
      sources_with_video++;
    }
  }

  // GRID LAYOUT CHANGE DETECTION:
  // Check if the number of active video sources has changed
  // NOTE: We only UPDATE the count and SIGNAL the change via out parameter
  // Broadcasting CLEAR_CONSOLE must happen AFTER the new frames are written to buffers
  // to prevent race condition where CLEAR arrives before new frame is ready
  int previous_count = atomic_load(&g_previous_active_video_count);
  if (sources_with_video != previous_count) {
    // Use compare-and-swap to ensure only ONE thread detects the change
    if (atomic_compare_exchange_strong(&g_previous_active_video_count, &previous_count, sources_with_video)) {
      log_info(
          "Grid layout changing: %d -> %d active video sources - caller will broadcast clear AFTER buffering frame",
          previous_count, sources_with_video);
      if (out_grid_changed) {
        *out_grid_changed = true; // Signal to caller
      }
    }
  }

  // No active video sources - don't generate placeholder frames
  if (sources_with_video == 0) {
    // DEBUG: Track when no video sources are available
    static uint64_t no_sources_count = 0;
    no_sources_count++;
    if (no_sources_count % 30 == 0) { // Log every 30 occurrences (1 second at 30fps)
    }
    log_debug("Per-client %u: No video sources available - returning NULL frame", target_client_id);
    *out_size = 0;
    return NULL;
  }

  // Create composite image for multiple sources with grid layout (same logic as global version)
  image_t *composite = NULL;

  if (sources_with_video == 1) {
    // Find the single source with video
    image_t *single_source = NULL;
    for (int i = 0; i < source_count; i++) {
      if (sources[i].has_video && sources[i].image) {
        single_source = sources[i].image;
        break;
      }
    }

    if (!single_source) {
      log_error("Logic error: sources_with_video=1 but no source found");
      *out_size = 0;
      return NULL;
    }
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
      calculate_fit_dimensions_pixel(single_source->w, single_source->h, width, height, &composite_width_px,
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
      float src_aspect = (float)single_source->w / (float)single_source->h;
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
        image_resize(single_source, fitted);

        // Copy fitted image to center of composite
        for (int y = 0; y < fitted_height; y++) {
          for (int x = 0; x < fitted_width; x++) {
            int src_idx = (y * fitted_width) + x;
            int dst_x = x_offset + x;
            int dst_y = y_offset + y;
            int dst_idx = (dst_y * composite->w) + dst_x;

            if (dst_x >= 0 && dst_x < composite->w && dst_y >= 0 && dst_y < composite->h) {
              composite->pixels[dst_idx] = fitted->pixels[src_idx];
            }
          }
        }
        image_destroy_to_pool(fitted);
      }
    } else {
      // Normal modes: Simple resize to fitted dimensions
      image_resize(single_source, composite);
    }
  } else if (sources_with_video > 1) {
    // Multiple sources - create grid layout
    client_info_t *target_client = find_client_by_id_fast(target_client_id);
    bool use_half_block_multi = target_client && target_client->has_terminal_caps &&
                                target_client->terminal_caps.render_mode == RENDER_MODE_HALF_BLOCK;

    // Start with terminal width, will be adjusted for vertical layout
    int composite_width_px = width;
    int composite_height_px = use_half_block_multi ? height * 2 : height; // Will fix this after grid calculation

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

    // Calculate grid dimensions to maximize terminal space usage
    int grid_cols, grid_rows;

    if (sources_with_video == 0) {
      grid_cols = 0;
      grid_rows = 0;
    } else if (sources_with_video == 1) {
      // Single client uses full terminal
      grid_cols = 1;
      grid_rows = 1;
    } else if (sources_with_video == 2) {
      // For 2 clients, choose between 1x2 and 2x1 based on aspect ratio

      // Calculate aspect ratios for both layouts
      // 1x2 (vertical split): each cell is width x (height/2)
      float cell_aspect_1x2 = (float)width / ((float)height / 2.0f);

      // 2x1 (horizontal split): each cell is (width/2) x height
      float cell_aspect_2x1 = ((float)width / 2.0f) / (float)height;

      // Target aspect ratio for video (typically 2:1 for ASCII)
      float target_aspect = 2.0f;

      // Choose layout that gives cells closest to target aspect ratio
      float diff_1x2 = fabsf(cell_aspect_1x2 - target_aspect);
      float diff_2x1 = fabsf(cell_aspect_2x1 - target_aspect);

      if (diff_1x2 <= diff_2x1) {
        // 1x2 layout (stacked vertically)
        grid_cols = 1;
        grid_rows = 2;
      } else {
        // 2x1 layout (side by side)
        grid_cols = 2;
        grid_rows = 1;
      }
    } else {
      // For 3+ clients, calculate optimal grid
      // Try to maintain reasonable aspect ratios for each cell
      int best_cols = 1;
      int best_rows = sources_with_video;
      float best_aspect_diff = INFINITY;
      float target_aspect = 2.0f; // Target aspect ratio for ASCII display

      // Try different grid configurations
      for (int cols = 1; cols <= sources_with_video; cols++) {
        int rows = (sources_with_video + cols - 1) / cols; // Ceiling division

        // Skip if this configuration has too many empty cells
        int total_cells = cols * rows;
        int empty_cells = total_cells - sources_with_video;
        if (empty_cells > cols)
          continue; // Don't waste more than one row

        // Calculate cell dimensions for this configuration
        int cell_width = width / cols;
        int cell_height = height / rows;

        // Skip if cells would be too small
        if (cell_width < 20 || cell_height < 10)
          continue;

        // Calculate aspect ratio of cells
        float cell_aspect = (float)cell_width / (float)cell_height;
        float aspect_diff = fabsf(cell_aspect - target_aspect);

        // Prefer configurations with better aspect ratios
        if (aspect_diff < best_aspect_diff) {
          best_aspect_diff = aspect_diff;
          best_cols = cols;
          best_rows = rows;
        }
      }

      grid_cols = best_cols;
      grid_rows = best_rows;
    }

    // Calculate cell dimensions in characters (for layout)
    int cell_width_chars = width / grid_cols;
    int cell_height_chars = height / grid_rows;

    // Convert cell dimensions to pixels for image operations
    int cell_width_px = cell_width_chars; // 1:1 mapping

    // Calculate cell height in pixels based on grid type
    int cell_height_px;
    int required_height;
    if (sources_with_video == 1) {
      // Single client: use full terminal dimensions
      cell_height_px = height;
      required_height = height;
    } else if (sources_with_video == 2 && grid_cols == 1 && grid_rows == 2) {
      // Vertical 1x2 layout: each cell gets full terminal height
      cell_height_px = height;                      // Use full terminal height per cell
      required_height = grid_rows * cell_height_px; // Total composite height
    } else {
      // Multi-client grid: use character-based height
      cell_height_px = cell_height_chars; // 1:1 mapping for grid layouts
      required_height = grid_rows * cell_height_px;
    }

    // Debug logging with actual cell dimensions used
    float terminal_aspect_ratio = (float)width / (float)height;
    log_info("Grid calculation: %d sources with video, terminal %dx%d (aspect %.2f), grid %dx%d, actual_cells %dx%d",
             sources_with_video, width, height, terminal_aspect_ratio, grid_cols, grid_rows, cell_width_px,
             cell_height_px);

    // For vertical layout, we'll adjust composite width after calculating target size
    int optimal_width_for_vertical; // Will be set later

    if (composite->h != required_height || composite->w != composite_width_px) {
      // Recreate composite with correct dimensions
      image_destroy_to_pool(composite);
      composite = image_new_from_pool(composite_width_px, required_height);
      if (!composite) {
        log_error("Failed to recreate composite with correct height");
        for (int i = 0; i < source_count; i++) {
          image_destroy_to_pool(sources[i].image);
        }
        return NULL;
      }
      image_clear(composite);
    }

    // Place each source in the grid
    int video_source_index = 0;                                        // Track only sources with video
    for (int i = 0; i < source_count && video_source_index < 9; i++) { // Max 9 sources in 3x3 grid
      if (!sources[i].image)
        continue;

      int row = video_source_index / grid_cols;
      int col = video_source_index % grid_cols;
      video_source_index++;

      // Use actual composite dimensions for cell calculations, since composite
      // may be recreated with different width (e.g. for vertical layouts)
      int actual_cell_width_px = composite->w / grid_cols;
      int actual_cell_height_px = composite->h / grid_rows;

      // Calculate cell position in pixels
      int cell_x_offset_px = col * actual_cell_width_px;
      int cell_y_offset_px = row * actual_cell_height_px;

      log_info(
          "GRID PLACEMENT: source_i=%d, video_idx=%d, row=%d, col=%d, cell_offset=(%d,%d), cell_size=%dx%d, grid=%dx%d",
          i, video_source_index - 1, row, col, cell_x_offset_px, cell_y_offset_px, actual_cell_width_px,
          actual_cell_height_px, grid_cols, grid_rows);

      float src_aspect = (float)sources[i].image->w / (float)sources[i].image->h;
      float cell_aspect = (float)actual_cell_width_px / (float)actual_cell_height_px;

      int target_width_px, target_height_px;

      // For 2-client vertical layout, use proper aspect-ratio fitting that maximizes space
      if (sources_with_video == 2 && grid_cols == 1 && grid_rows == 2) {
        // Calculate both possible scalings
        int width_constrained_w = actual_cell_width_px;
        int width_constrained_h = (int)((actual_cell_width_px / src_aspect) + 0.5f);

        int height_constrained_h = actual_cell_height_px;
        int height_constrained_w = (int)((actual_cell_height_px * src_aspect) + 0.5f);

        // For vertical layout, prefer width-constrained to maximize usage of available space
        if (width_constrained_h <= actual_cell_height_px) {
          // Width-constrained scaling fits and gives better space utilization
          target_width_px = width_constrained_w;
          target_height_px = width_constrained_h;
        } else {
          // Fall back to height-constrained if width-constrained is too tall
          target_width_px = height_constrained_w;
          target_height_px = height_constrained_h;
        }

        // For vertical layout, adjust composite width to match content width to eliminate gaps
        if (video_source_index == 1) { // First source, set the composite width
          optimal_width_for_vertical = target_width_px;
          // Recreate composite with optimal width
          if (composite->w != optimal_width_for_vertical) {
            image_destroy_to_pool(composite);
            composite = image_new_from_pool(optimal_width_for_vertical, required_height);
            if (!composite) {
              log_error("Failed to recreate composite with optimal width");
              for (int j = 0; j < source_count; j++) {
                image_destroy_to_pool(sources[j].image);
              }
              return NULL;
            }
            image_clear(composite);
            // Recalculate cell dimensions after composite recreation
            actual_cell_width_px = composite->w / grid_cols;
            actual_cell_height_px = composite->h / grid_rows;
          }
        }
      } else if (sources_with_video == 1) {
        // Single client: maximize space while respecting aspect ratio
        // Try both width-constrained and height-constrained scaling
        int width_constrained_w = actual_cell_width_px;
        int width_constrained_h = (int)((actual_cell_width_px / src_aspect) + 0.5f);

        int height_constrained_h = actual_cell_height_px;
        int height_constrained_w = (int)((actual_cell_height_px * src_aspect) + 0.5f);

        // Choose the scaling that uses the most space (largest area)
        int width_area = width_constrained_w * width_constrained_h;
        int height_area = height_constrained_w * height_constrained_h;

        if (width_constrained_h <= actual_cell_height_px && width_area >= height_area) {
          // Width-constrained scaling fits and gives equal or better space utilization
          target_width_px = width_constrained_w;
          target_height_px = width_constrained_h;
        } else if (height_constrained_w <= actual_cell_width_px) {
          // Height-constrained scaling fits
          target_width_px = height_constrained_w;
          target_height_px = height_constrained_h;
        } else {
          // Fallback to width-constrained if height-constrained doesn't fit
          target_width_px = width_constrained_w;
          target_height_px = width_constrained_h;
        }
      } else {
        // Normal aspect ratio fitting for other layouts
        if (src_aspect > cell_aspect) {
          // Image is wider than cell - fit to width
          target_width_px = actual_cell_width_px;
          target_height_px = (int)((actual_cell_width_px / src_aspect) + 0.5f);
        } else {
          // Image is taller than cell - fit to height
          target_height_px = actual_cell_height_px;
          target_width_px = (int)((actual_cell_height_px * src_aspect) + 0.5f);
        }
      }

      // Ensure target dimensions don't exceed cell dimensions
      if (target_width_px > actual_cell_width_px)
        target_width_px = actual_cell_width_px;
      if (target_height_px > actual_cell_height_px)
        target_height_px = actual_cell_height_px;

      // Create resized image with standard allocation
      image_t *resized = image_new_from_pool(target_width_px, target_height_px);
      if (resized) {
        image_resize(sources[i].image, resized);

        // Grid centering strategy:
        // - Multi-client: Apply padding ONLY to edge cells to center the grid as a whole
        //   (left column gets left padding, right column gets right padding)
        //   (top row gets top padding, bottom row gets bottom padding)
        //   This keeps cells edge-to-edge while centering the entire grid
        // - Single client: Center the image within the cell
        int x_padding_px, y_padding_px;

        if (sources_with_video > 1) {
          // Multi-client grid: edge-only padding for grid centering
          int base_x_padding = (actual_cell_width_px - target_width_px) / 2;
          int base_y_padding = (actual_cell_height_px - target_height_px) / 2;

          // Apply left padding only to leftmost column (col == 0)
          x_padding_px = (col == 0) ? base_x_padding : 0;
          // Apply top padding only to top row (row == 0)
          y_padding_px = (row == 0) ? base_y_padding : 0;
        } else {
          // Single client: center within cell
          x_padding_px = (actual_cell_width_px - target_width_px) / 2;
          y_padding_px = (actual_cell_height_px - target_height_px) / 2;
        }

        log_info(
            "COPYING: target_size=%dx%d, padding=(%d,%d), final_dst_range=(%d,%d) to (%d,%d), composite_size=%dx%d",
            target_width_px, target_height_px, x_padding_px, y_padding_px, cell_x_offset_px + x_padding_px,
            cell_y_offset_px + y_padding_px, cell_x_offset_px + x_padding_px + target_width_px - 1,
            cell_y_offset_px + y_padding_px + target_height_px - 1, composite->w, composite->h);

        // Copy resized image to composite with proper bounds checking
        for (int y = 0; y < target_height_px; y++) {
          for (int x = 0; x < target_width_px; x++) {
            int src_idx = (y * target_width_px) + x;
            int dst_x = cell_x_offset_px + x_padding_px + x;
            int dst_y = cell_y_offset_px + y_padding_px + y;
            int dst_idx = (dst_y * composite->w) + dst_x;

            // Bounds checking with correct composite dimensions
            bool src_ok = src_idx >= 0 && src_idx < resized->w * resized->h;
            bool dst_idx_ok = dst_idx >= 0 && dst_idx < composite->w * composite->h;
            bool dst_x_ok = dst_x >= 0 && dst_x < composite->w;
            bool dst_y_ok = dst_y >= 0 && dst_y < composite->h;

            if (src_ok && dst_idx_ok && dst_x_ok && dst_y_ok) {
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

  if (target_client) {
    // CRITICAL FIX: Follow lock ordering protocol - acquire rwlock first, then client mutex
    // This prevents deadlocks when called concurrently with other functions that follow proper ordering
    rwlock_rdlock(&g_client_manager_rwlock);
    mutex_lock(&target_client->client_state_mutex);
    uint32_t client_id_snapshot = atomic_load(&target_client->client_id);
    bool has_terminal_caps_snapshot = target_client->has_terminal_caps;
    terminal_capabilities_t caps_snapshot = target_client->terminal_caps;
    mutex_unlock(&target_client->client_state_mutex);
    rwlock_rdunlock(&g_client_manager_rwlock);

    if (client_id_snapshot != 0 && has_terminal_caps_snapshot) {
      if (target_client->client_palette_initialized) {
        // Render with client's custom palette using enhanced capabilities
        if (caps_snapshot.render_mode == RENDER_MODE_HALF_BLOCK) {
          ascii_frame = ascii_convert_with_capabilities(composite, width, height * 2, &caps_snapshot, true, false,
                                                        target_client->client_palette_chars,
                                                        target_client->client_luminance_palette);
        } else {
          // Use terminal dimensions for ASCII conversion to ensure full screen usage
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
  } else {
    // Target client not found
    log_debug("Per-client %u: Target client not found", target_client_id);
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
// REMOVED: queue_ascii_frame_for_client - video now uses double buffer directly in client->outgoing_video_buffer

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

/**
 * @brief Check if any connected clients are currently sending video
 *
 * This function scans all active clients to determine if at least one is
 * sending video frames. Used by render threads to avoid generating frames
 * when no video sources are available (e.g., during webcam warmup).
 *
 * @return true if at least one client has is_sending_video flag set, false otherwise
 */
bool any_clients_sending_video(void) {
  bool has_video = false;

  // Acquire read lock to check all clients
  rwlock_rdlock(&g_client_manager_rwlock);

  // Iterate through all client slots
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

    // Skip uninitialized clients
    if (atomic_load(&client->client_id) == 0) {
      continue;
    }

    // Check if client is active and sending video
    if (atomic_load(&client->active) && atomic_load(&client->is_sending_video)) {
      has_video = true;
      break;
    }
  }

  rwlock_rdunlock(&g_client_manager_rwlock);

  return has_video;
}
