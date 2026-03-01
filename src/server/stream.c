/**
 * @file server/stream.c
 * @ingroup server_stream
 * @brief 🎬 Multi-client video mixer: frame generation, ASCII conversion, and per-client personalized rendering
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
 * - video/: Performs actual RGB-to-ASCII conversion
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
 * @see video/ For RGB-to-ASCII conversion implementation
 * @see palette.c For client palette management
 */

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <float.h>

#include "main.h"
#include "stream.h"
#include "client.h"
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/ringbuffer.h>
#include <ascii-chat/video/video_frame.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/color_filter.h>
#include <ascii-chat/util/aspect_ratio.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/image.h>

/**
 * @brief Previous active video source count for layout change detection
 *
 * Tracks the number of active video sources from the previous frame generation
 * cycle. Used to detect changes in the active client count, which triggers
 * grid layout recalculation for optimal display arrangement.
 *
 * @ingroup server_stream
 */
static atomic_int g_previous_active_video_count = 0;

/**
 * @brief Image source structure for multi-client video mixing
 *
 * Represents a single video source (client) in the video mixing pipeline.
 * This structure is used to collect video frames from all active clients
 * before creating composite layouts for multi-user display.
 * ============
 * - image: Pointer to the client's current video frame (image_t structure)
 * - client_id: Unique identifier for this client
 * - has_video: Whether this client is actively sending video
 *
 * USAGE PATTERN:
 * ==============
 * 1. Collect video sources: collect_video_sources() fills array with active clients
 * 2. Filter sources: Only sources with has_video=true are used in composite
 * 3. Create composite: generate_composite_frame() uses sources to create layout
 * 4. Free sources: Sources are automatically cleaned up after composite generation
 *
 * VIDEO MIXING:
 * =============
 * This structure is central to the multi-client video mixing system:
 * - Single client: One source, full-screen display
 * - Multiple clients: Multiple sources, grid layout (2x2, 3x3, etc.)
 * - Grid layout: Each source occupies one cell in the grid
 * - Aspect ratio: Each source maintains its aspect ratio within cell
 *
 * MEMORY MANAGEMENT:
 * ==================
 * - image pointer points to frame data managed by video_frame_buffer_t
 * - Frame data is owned by the double-buffer system, not this structure
 * - No manual memory management needed (automatic via buffer system)
 *
 * @note image pointer is valid only when has_video is true.
 * @note image pointer may be NULL if client stopped sending video.
 * @note client_id is used to identify which client this source represents.
 *
 * @ingroup server_stream
 */
typedef struct {
  /** @brief Pointer to client's current video frame (owned by buffer system) */
  image_t *image;
  /** @brief Unique client identifier for this source */
  char client_id[MAX_CLIENT_ID_LEN];
  /** @brief Whether this client has active video stream */
  bool has_video;
} image_source_t;

/**
 * @brief Collect video frames from all active clients
 * @param sources Output array of image sources
 * @param max_sources Maximum number of sources to collect
 * @return Number of sources collected
 * @ingroup server_stream
 */
static int collect_video_sources(image_source_t *sources, int max_sources) {
  int source_count = 0;

  // Check for shutdown before acquiring locks to prevent lock corruption
  if (atomic_load(&g_server_should_exit)) {
    return 0;
  }

  // Collect client info snapshots WITHOUT holding rwlock
  typedef struct {
    char client_id[MAX_CLIENT_ID_LEN];
    bool is_active;
    bool is_sending_video;
    video_frame_buffer_t *video_buffer;
  } client_snapshot_t;

  client_snapshot_t client_snapshots[MAX_CLIENTS];
  int snapshot_count = 0;

  // NO LOCK: All fields are atomic or stable pointers
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

    if (client->client_id[0] == '\0') {
      continue; // Skip uninitialized clients
    }

    // Snapshot all needed client state (all atomic reads or stable pointers)
    SAFE_STRNCPY(client_snapshots[snapshot_count].client_id, client->client_id,
                 sizeof(client_snapshots[snapshot_count].client_id) - 1);
    client_snapshots[snapshot_count].is_active = atomic_load(&client->active);
    client_snapshots[snapshot_count].is_sending_video = atomic_load(&client->is_sending_video);
    client_snapshots[snapshot_count].video_buffer = client->incoming_video_buffer; // Stable pointer
    snapshot_count++;
  }

  // Process frames (expensive operations)
  log_dev_every(5 * NS_PER_MS_INT, "collect_video_sources: Processing %d snapshots", snapshot_count);
  for (int i = 0; i < snapshot_count && source_count < max_sources; i++) {
    client_snapshot_t *snap = &client_snapshots[i];

    if (!snap->is_active) {
      log_dev_every(5 * NS_PER_MS_INT, "collect_video_sources: Skipping inactive client %u", snap->client_id);
      continue;
    }

    log_dev_every(5 * NS_PER_MS_INT, "collect_video_sources: Client %s: is_sending_video=%d", snap->client_id,
                  snap->is_sending_video);

    SAFE_STRNCPY(sources[source_count].client_id, snap->client_id, sizeof(sources[source_count].client_id) - 1);
    sources[source_count].image = NULL; // Will be set if video is available
    sources[source_count].has_video = false;

    // Declare these outside the if block so they're accessible later
    multi_source_frame_t current_frame = {0};
    bool got_new_frame = false;

    // Always try to get the last available video frame for consistent ASCII generation
    // The double buffer ensures we always have the last valid frame
    if (snap->is_sending_video && snap->video_buffer) {
      // Get the latest frame (always available from double buffer)
      const video_frame_t *frame = video_frame_get_latest(snap->video_buffer);

      if (!frame) {
        continue; // Skip to next snapshot
      }

      // Try to access frame fields ONE AT A TIME to pinpoint the hang
      void *frame_data_ptr = frame->data;

      size_t frame_size_val = frame->size;

      // Compute hash of incoming frame to verify it's changing
      uint32_t incoming_hash = 0;
      if (frame_data_ptr && frame_size_val > 0) {
        for (size_t i = 0; i < frame_size_val && i < 1000; i++) {
          uint8_t byte = ((unsigned char *)frame_data_ptr)[i];
          incoming_hash = (uint32_t)((uint64_t)incoming_hash * 31 + byte);
        }
      }

      // DIAGNOSTIC: Track incoming frame changes from buffer
      static uint32_t last_buffer_hash = 0;
      if (incoming_hash != last_buffer_hash) {
        log_info("BUFFER_FRAME CHANGE: Client %u got NEW frame from buffer: hash=0x%08x (prev=0x%08x) size=%zu",
                 snap->client_id, incoming_hash, last_buffer_hash, frame_size_val);
        last_buffer_hash = incoming_hash;
      } else {
        log_dev_every(25000, "BUFFER_FRAME DUPLICATE: Client %u frame hash=0x%08x size=%zu (no change)",
                      snap->client_id, incoming_hash, frame_size_val);
      }

      // DETAILED BUFFER INSPECTION: Extract and log frame dimensions + first pixels
      if (frame_data_ptr && frame_size_val >= 8) {
        uint32_t width_net, height_net;
        memcpy(&width_net, frame_data_ptr, sizeof(uint32_t));
        memcpy(&height_net, (char *)frame_data_ptr + sizeof(uint32_t), sizeof(uint32_t));
        uint32_t width = NET_TO_HOST_U32(width_net);
        uint32_t height = NET_TO_HOST_U32(height_net);

        // Extract first 3 RGB pixels to inspect actual pixel data
        uint8_t *pixel_ptr = (uint8_t *)frame_data_ptr + 8;
        uint32_t first_pixel_rgb = 0;
        if (frame_size_val >= 11) {
          first_pixel_rgb = ((uint32_t)pixel_ptr[0] << 16) | ((uint32_t)pixel_ptr[1] << 8) | (uint32_t)pixel_ptr[2];
        }

        log_info("BUFFER_INSPECT: Client %u dims=%ux%u pixel_data_size=%zu first_pixel_rgb=0x%06x data_hash=0x%08x",
                 snap->client_id, width, height, frame_size_val - 8, first_pixel_rgb, incoming_hash);
      }

      log_debug_every(5 * NS_PER_MS_INT, "Video mixer: client %u incoming frame hash=0x%08x size=%zu", snap->client_id,
                      incoming_hash, frame_size_val);

      if (frame_data_ptr && frame_size_val > 0 && frame_size_val >= (sizeof(uint32_t) * 2 + 3)) {
        // PARSE AND VALIDATE DIMENSIONS BEFORE COPYING
        // Don't trust frame->size - calculate correct size from dimensions
        uint32_t peek_width = NET_TO_HOST_U32(read_u32_unaligned(frame_data_ptr));
        uint32_t peek_height = NET_TO_HOST_U32(read_u32_unaligned(frame_data_ptr + sizeof(uint32_t)));

        // Reject obviously corrupted dimensions
        if (peek_width == 0 || peek_height == 0 || peek_width > 4096 || peek_height > 2160) {
          log_debug("Per-client %u: rejected dimensions %ux%u as corrupted", snap->client_id, peek_width, peek_height);
          continue;
        }

        // Validate dimensions
        if (image_validate_dimensions((size_t)peek_width, (size_t)peek_height) != ASCIICHAT_OK) {
          continue;
        }

        // Calculate CORRECT frame size based on dimensions (don't trust frame->size)
        size_t correct_frame_size = sizeof(uint32_t) * 2;
        {
          size_t rgb_size = 0;
          if (image_calc_rgb_size((size_t)peek_width, (size_t)peek_height, &rgb_size) != ASCIICHAT_OK) {
            log_debug("Per-client: rgb_size calc failed for %ux%u", peek_width, peek_height);
            continue;
          }
          correct_frame_size += rgb_size;
        }

        log_debug_every(NS_PER_MS_INT, "Per-client: frame dimensions=%ux%u, frame_size=%zu, correct_size=%zu",
                        peek_width, peek_height, frame_size_val, correct_frame_size);

        // Verify frame is at least large enough for the correct size
        if (frame_size_val < correct_frame_size) {
          log_debug("Per-client: frame too small: got %zu, need %zu", frame_size_val, correct_frame_size);
          continue;
        }

        // We have frame data - copy ONLY the correct amount based on dimensions
        // Use SAFE_MALLOC (not buffer pool - image_new_from_pool uses pool and causes overlap)
        current_frame.data = SAFE_MALLOC(correct_frame_size, void *);

        if (current_frame.data) {
          memcpy(current_frame.data, frame->data, correct_frame_size);
          current_frame.size = correct_frame_size;
          SAFE_STRNCPY(current_frame.source_client_id, snap->client_id, sizeof(current_frame.source_client_id) - 1);
          current_frame.timestamp = (uint32_t)(frame->capture_timestamp_ns / NS_PER_SEC_INT);
          got_new_frame = true;
        }
      } else {
      }
    }

    multi_source_frame_t *frame_to_use = got_new_frame ? &current_frame : NULL;

    if (frame_to_use && frame_to_use->data && frame_to_use->size > sizeof(uint32_t) * 2) {
      // Parse the image data
      // Format: [width:4][height:4][rgb_data:w*h*3]
      // Use unaligned read helpers - frame data may not be aligned
      uint32_t img_width = NET_TO_HOST_U32(read_u32_unaligned(frame_to_use->data));
      uint32_t img_height = NET_TO_HOST_U32(read_u32_unaligned(frame_to_use->data + sizeof(uint32_t)));

      // Debug logging to understand the data
      if (img_width == 0xBEBEBEBE || img_height == 0xBEBEBEBE) {
        SET_ERRNO(ERROR_INVALID_STATE, "UNINITIALIZED MEMORY DETECTED! First 16 bytes of frame data:");
        uint8_t *bytes = (uint8_t *)frame_to_use->data;
        SET_ERRNO(ERROR_INVALID_STATE,
                  "  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", bytes[0],
                  bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
                  bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
      }

      // Validate dimensions using image utility function
      if (image_validate_dimensions((size_t)img_width, (size_t)img_height) != ASCIICHAT_OK) {
        SET_ERRNO(ERROR_INVALID_STATE,
                  "Per-client: Invalid image dimensions from client %u: %ux%u (data may be corrupted)", snap->client_id,
                  img_width, img_height);
        source_count++;
        continue;
      }

      // Calculate expected frame size with overflow checking
      size_t expected_size = sizeof(uint32_t) * 2;
      {
        size_t rgb_size = 0;
        if (image_calc_rgb_size((size_t)img_width, (size_t)img_height, &rgb_size) != ASCIICHAT_OK) {
          SET_ERRNO(ERROR_INVALID_STATE, "Per-client: RGB size calculation failed for client %u: %ux%u",
                    snap->client_id, img_width, img_height);
          source_count++;
          continue;
        }
        expected_size += rgb_size;
      }
      if (frame_to_use->size != expected_size) {
        SET_ERRNO(ERROR_INVALID_STATE,
                  "Per-client: Frame size mismatch from client %u: got %zu, expected %zu for %ux%u image",
                  snap->client_id, frame_to_use->size, expected_size, img_width, img_height);
        source_count++;
        continue;
      }

      // Extract pixel data pointer
      rgb_pixel_t *pixels = (rgb_pixel_t *)(frame_to_use->data + (sizeof(uint32_t) * 2));

      // Create image from buffer pool for consistent video pipeline management
      image_t *img = image_new_from_pool(img_width, img_height);
      if (!img) {
        log_error("Per-client: image_new_from_pool failed for %ux%u", img_width, img_height);
        continue;
      }
      memcpy(img->pixels, pixels, (size_t)img_width * (size_t)img_height * sizeof(rgb_pixel_t));
      sources[source_count].image = img;
      sources[source_count].has_video = true;

      // Free temporary frame buffer - image has its own pixel data now
      if (got_new_frame) {
        SAFE_FREE(current_frame.data);
      }
    } else {
      // frame_to_use check failed - clean up allocated frame data
      if (got_new_frame && current_frame.data) {
        SAFE_FREE(current_frame.data);
      }
    }

    // Increment source count for this active client (with or without video)
    source_count++;
  }

  return source_count;
}

/**
 * @brief Create composite image for single video source layout
 * @param sources Array of image sources
 * @param source_count Number of sources in array
 * @param target_client_id Client ID receiving this composite
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return Composite image or NULL on error
 * @ingroup server_stream
 */
static image_t *create_single_source_composite(image_source_t *sources, int source_count,
                                               const char *target_client_id __attribute__((unused)),
                                               unsigned short width __attribute__((unused)),
                                               unsigned short height __attribute__((unused))) {
  // Find the single source with video
  image_t *single_source = NULL;
  for (int i = 0; i < source_count; i++) {
    if (sources[i].has_video && sources[i].image) {
      single_source = sources[i].image;
      break;
    }
  }

  if (!single_source) {
    SET_ERRNO(ERROR_INVALID_STATE, "Logic error: sources_with_video=1 but no source found");
    return NULL;
  }

  // For single source, don't pre-fit the image. Let ascii_convert_with_capabilities handle
  // all aspect ratio fitting with proper CHAR_ASPECT correction. This avoids double-correction
  // that was happening when the image was pre-fitted in pixel space and then aspect_ratio()
  // was called again with CHAR_ASPECT=2.0.
  // Just return the source image as-is; ascii_convert_with_capabilities will fit it properly.
  return single_source;
}

/**
 * @brief Calculate optimal grid layout that maximizes space usage
 *
 * This function tries all reasonable grid configurations (rows x cols) and chooses
 * the one that uses the most terminal space while respecting video aspect ratios.
 *
 * Algorithm:
 * 1. Try all grid configurations from 1x1 to NxN
 * 2. For each configuration, calculate total area used by fitting videos into cells
 * 3. Choose configuration with highest total area utilization
 *
 * @param sources Array of video sources with aspect ratio information
 * @param source_count Size of sources array
 * @param sources_with_video Number of active video sources
 * @param terminal_width Terminal width in characters
 * @param terminal_height Terminal height in characters
 * @param out_cols Output: optimal number of columns
 * @param out_rows Output: optimal number of rows
 *
 * @ingroup server_stream
 */
static void calculate_optimal_grid_layout(image_source_t *sources, int source_count, int sources_with_video,
                                          int terminal_width, int terminal_height, int *out_cols, int *out_rows) {
  // Special cases
  if (sources_with_video == 0) {
    *out_cols = 0;
    *out_rows = 0;
    return;
  }

  if (sources_with_video == 1) {
    *out_cols = 1;
    *out_rows = 1;
    return;
  }

  // ASCII character aspect ratio: characters are ~2x taller than wide
  // So we need to adjust terminal dimensions to visual space
  const float CHAR_ASPECT = 2.0f; // Character height / width ratio

  // Calculate average aspect ratio of all video sources
  float avg_aspect = 0.0f;
  int aspect_count = 0;
  for (int i = 0; i < source_count; i++) {
    if (sources[i].has_video && sources[i].image) {
      float aspect = (float)sources[i].image->w / (float)sources[i].image->h;
      avg_aspect += aspect;
      aspect_count++;
    }
  }
  if (aspect_count > 0) {
    avg_aspect /= aspect_count;
  } else {
    avg_aspect = 1.6f; // Default aspect ratio
  }

  // Try all reasonable grid configurations
  int best_cols = 1;
  int best_rows = sources_with_video;
  float best_utilization = 0.0f;

  // Try grid configurations from 1x1 up to reasonable limits
  for (int cols = 1; cols <= sources_with_video; cols++) {
    int rows = (sources_with_video + cols - 1) / cols; // Ceiling division

    // Skip configurations with too many empty cells (more than one row worth)
    int total_cells = cols * rows;
    int empty_cells = total_cells - sources_with_video;
    if (empty_cells > cols) {
      continue;
    }

    // Calculate cell dimensions for this configuration
    int cell_width = terminal_width / cols;
    int cell_height = terminal_height / rows;

    // Skip if cells would be too small
    if (cell_width < 20 || cell_height < 10) {
      continue;
    }

    // Calculate total area utilized by all videos in this configuration
    // For each video, calculate how much space it would use in a cell
    // IMPORTANT: Account for character aspect ratio (chars are 2x taller than wide)
    float total_area_used = 0.0f;
    int cell_area = cell_width * cell_height;

    for (int i = 0; i < sources_with_video; i++) {
      // Use average aspect ratio for calculation
      float video_aspect = avg_aspect;

      // Calculate VISUAL cell aspect ratio (accounting for character shape)
      // A cell that is cell_width chars wide and cell_height chars tall
      // has visual aspect = cell_width / (cell_height * CHAR_ASPECT)
      float cell_visual_aspect = (float)cell_width / ((float)cell_height * CHAR_ASPECT);

      // Calculate fitted dimensions while preserving aspect ratio
      int fitted_width, fitted_height;

      if (video_aspect > cell_visual_aspect) {
        // Video is wider than cell - fit to width
        fitted_width = cell_width;
        // Visual height needed: cell_width / video_aspect
        // Character height: visual_height / CHAR_ASPECT
        fitted_height = (int)((cell_width / video_aspect) / CHAR_ASPECT);
      } else {
        // Video is taller than cell - fit to height
        fitted_height = cell_height;
        // Visual width needed: cell_height * CHAR_ASPECT * video_aspect
        fitted_width = (int)(cell_height * CHAR_ASPECT * video_aspect);
      }

      // Clamp to cell bounds
      if (fitted_width > cell_width) {
        fitted_width = cell_width;
      }
      if (fitted_height > cell_height) {
        fitted_height = cell_height;
      }

      // Add area used by this video
      total_area_used += fitted_width * fitted_height;
    }

    // Calculate utilization percentage
    float total_available_area = (float)(cell_area * sources_with_video);
    float utilization = total_area_used / total_available_area;

    float test_cell_visual_aspect = (float)cell_width / ((float)cell_height * CHAR_ASPECT);
    log_dev_every(LOG_RATE_NORMAL, "  Testing %dx%d: cell=%dx%d (visual aspect %.2f), utilization=%.1f%%", cols, rows,
                  cell_width, cell_height, test_cell_visual_aspect, utilization * 100.0f);

    // Prefer configurations with better utilization
    if (utilization > best_utilization) {
      best_utilization = utilization;
      best_cols = cols;
      best_rows = rows;
    }
  }

  *out_cols = best_cols;
  *out_rows = best_rows;

  float terminal_visual_aspect = (float)terminal_width / ((float)terminal_height * CHAR_ASPECT);
  log_dev_every(LOG_RATE_NORMAL,
                "Grid layout: %d clients -> %dx%d grid (%.1f%% utilization) | terminal=%dx%d (char aspect %.2f, VISUAL "
                "aspect %.2f), video aspect: %.2f",
                sources_with_video, best_cols, best_rows, best_utilization * 100.0f, terminal_width, terminal_height,
                (float)terminal_width / (float)terminal_height, terminal_visual_aspect, avg_aspect);
}

/**
 * @brief Create composite image for multi-source grid layout
 * @param sources Array of image sources
 * @param source_count Number of sources in array
 * @param sources_with_video Number of sources with active video
 * @param target_client_id Client ID receiving this composite (unused)
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return Composite image or NULL on error
 * @ingroup server_stream
 */
static image_t *create_multi_source_composite(image_source_t *sources, int source_count, int sources_with_video,
                                              const char *target_client_id, unsigned short width,
                                              unsigned short height) {
  (void)target_client_id; // Unused - composite is same for all clients now

  // Calculate optimal grid layout using space-maximizing algorithm
  int grid_cols, grid_rows;
  calculate_optimal_grid_layout(sources, source_count, sources_with_video, width, height, &grid_cols, &grid_rows);

  // Calculate composite dimensions in PIXELS for half-block mode
  // Terminal dimensions are in CHARACTERS, need to convert to pixels:
  // - Width: 1 char = 1 horizontal pixel
  // - Height: 1 char = 2 vertical pixels (half-block = 2 pixels per char)
  const int PIXELS_PER_CHAR_HEIGHT = 2;
  int composite_width_px = width;                            // chars = pixels horizontally
  int composite_height_px = height * PIXELS_PER_CHAR_HEIGHT; // chars * 2 = pixels vertically

  // Create composite with final dimensions - no recreation needed
  image_t *composite = image_new_from_pool(composite_width_px, composite_height_px);
  image_clear(composite);

  // Place each source in the grid
  int video_source_index = 0;                                        // Track only sources with video
  for (int i = 0; i < source_count && video_source_index < 9; i++) { // Max 9 sources in 3x3 grid
    if (!sources[i].image)
      continue;

    int row = video_source_index / grid_cols;
    int col = video_source_index % grid_cols;
    video_source_index++;

    // Use actual composite dimensions for cell calculations
    // Composite is now in PIXELS (already converted from terminal characters)
    int cell_width_px = composite->w / grid_cols;
    int cell_height_px = composite->h / grid_rows;

    // Calculate aspect ratios
    float src_aspect = (float)sources[i].image->w / (float)sources[i].image->h;
    float cell_visual_aspect = (float)cell_width_px / (float)cell_height_px;

    int target_width_px, target_height_px;

    // CONTAIN strategy: Fill one dimension completely, let other scale down (never overflow)
    // Compare aspects to decide which dimension to fill
    if (src_aspect > cell_visual_aspect) {
      // Video is WIDER than cell → fill WIDTH (height will be smaller)
      target_width_px = cell_width_px;
      target_height_px = (int)((cell_width_px / src_aspect) + 0.5f);
    } else {
      // Video is TALLER than cell → fill HEIGHT (width will be smaller)
      target_height_px = cell_height_px;
      target_width_px = (int)((cell_height_px * src_aspect) + 0.5f);
    }

    log_dev_every(LOG_RATE_NORMAL, "Cell %d: %dx%d px, video %.1f, cell %.2f → target %dx%d px (fill %s)",
                  video_source_index - 1, cell_width_px, cell_height_px, src_aspect, cell_visual_aspect,
                  target_width_px, target_height_px, (src_aspect > cell_visual_aspect) ? "WIDTH" : "HEIGHT");

    // Create resized image with standard allocation
    image_t *resized = image_new_from_pool(target_width_px, target_height_px);
    image_resize(sources[i].image, resized);

    // Calculate cell position in pixels (after any composite recreation)
    int cell_x_offset_px = col * cell_width_px;
    int cell_y_offset_px = row * cell_height_px;

    // Grid centering strategy:
    // - Multi-client: Apply padding ONLY to edge cells to center the grid as a whole
    //   (left column gets left padding, right column gets right padding)
    //   (top row gets top padding, bottom row gets bottom padding)
    //   This keeps cells edge-to-edge while centering the entire grid
    // - Single client: Center the image within the cell
    int x_padding_px, y_padding_px;

    // Center images within their cells for all layouts
    // This prevents gaps/stripes between clients
    x_padding_px = (cell_width_px - target_width_px) / 2;
    y_padding_px = (cell_height_px - target_height_px) / 2;

    // Define cell boundaries for clipping (prevents bleeding into adjacent cells)
    int cell_x_min = cell_x_offset_px;
    int cell_x_max = cell_x_offset_px + cell_width_px - 1;
    int cell_y_min = cell_y_offset_px;
    int cell_y_max = cell_y_offset_px + cell_height_px - 1;

    // Copy resized image to composite with cell boundary clipping
    // This allows images to fill cells completely while preventing overlap

    for (int y = 0; y < resized->h; y++) {
      for (int x = 0; x < resized->w; x++) {
        // Calculate destination position
        int dst_x = cell_x_offset_px + x_padding_px + x;
        int dst_y = cell_y_offset_px + y_padding_px + y;

        // Clip to cell boundaries (prevents bleeding into adjacent cells)
        if (dst_x < cell_x_min || dst_x > cell_x_max || dst_y < cell_y_min || dst_y > cell_y_max) {
          continue; // Skip pixels outside cell boundaries
        }

        // Additional composite boundary check
        if (dst_x < 0 || dst_x >= composite->w || dst_y < 0 || dst_y >= composite->h) {
          continue; // Skip pixels outside composite
        }

        // Copy pixel
        int src_idx = (y * resized->w) + x;
        int dst_idx = (dst_y * composite->w) + dst_x;
        composite->pixels[dst_idx] = resized->pixels[src_idx];
      }
    }

    image_destroy_to_pool(resized);
  }

  return composite;
}

/**
 * @brief Convert composite image to ASCII using client capabilities
 * @param composite Composite image to convert
 * @param target_client_id Client ID for capability lookup
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return Allocated ASCII frame string (caller must free) or NULL on error
 * @ingroup server_stream
 */
static char *convert_composite_to_ascii(image_t *composite, const char *target_client_id, unsigned short width,
                                        unsigned short height) {
  // LOCK OPTIMIZATION: Don't call find_client_by_id() - it would acquire rwlock unnecessarily
  // Instead, the render thread already has snapshot of client state, so we just need palette data
  // which is stable after initialization

  // We need to find the client to access palette data, but we can do this without locking
  // since palette is initialized once and never changes
  client_info_t *render_client = NULL;

  // Find client without locking - client_id is stable once set
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (strcmp(client->client_id, target_client_id) == 0) {
      render_client = client;
      break;
    }
  }

  if (!render_client) {
    SET_ERRNO(ERROR_INVALID_STATE, "Per-client %u: Target client not found", target_client_id);
    return NULL;
  }

  // Snapshot terminal capabilities WITHOUT holding rwlock
  // Terminal caps are set once during handshake and never change, so this is safe
  bool has_terminal_caps_snapshot = render_client->has_terminal_caps;
  if (!has_terminal_caps_snapshot) {
    SET_ERRNO(ERROR_INVALID_STATE, "Per-client %u: Terminal capabilities not received", target_client_id);
    return NULL;
  }

  terminal_capabilities_t caps_snapshot = render_client->terminal_caps;

  if (!render_client->client_palette_initialized) {
    SET_ERRNO(ERROR_TERMINAL, "Client %u palette not initialized - cannot render frame", target_client_id);
    return NULL;
  }

  // Render with client's custom palette using enhanced capabilities
  // Palette data is stable after initialization, so no locking needed
  const int h = caps_snapshot.render_mode == RENDER_MODE_HALF_BLOCK ? height * 2 : height;

  // DEBUG: Log dimensions being used for ASCII conversion
  log_dev_every(LOG_RATE_SLOW, "convert_composite_to_ascii: composite=%dx%d, terminal=%dx%d, h=%d (mode=%d)",
                composite->w, composite->h, width, height, h, caps_snapshot.render_mode);

  // Pass full terminal dimensions so ascii_convert_with_capabilities can fit the image correctly
  // with proper CHAR_ASPECT correction. The composite may have been pre-fitted in pixel space,
  // but ascii_convert will handle terminal character aspect ratio properly when aspect_ratio=true.
  uint64_t convert_start_ns = time_get_ns();
  char *ascii_frame = ascii_convert_with_capabilities(composite, width, h, &caps_snapshot, true, false,
                                                      render_client->client_palette_chars);
  uint64_t convert_end_ns = time_get_ns();
  uint64_t convert_duration_ns = convert_end_ns - convert_start_ns;

  if (convert_duration_ns > 5 * NS_PER_MS_INT) { // Log if > 5ms
    char duration_str[32];
    time_pretty((uint64_t)((double)convert_duration_ns), -1, duration_str, sizeof(duration_str));
    log_warn("SLOW_ASCII_CONVERT: Client %u took %s to convert %dx%d image to ASCII", target_client_id, duration_str,
             composite->w, composite->h);
  }

  return ascii_frame;
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
// Compute hash of all active video sources for cache invalidation
// Uses hardware-accelerated CRC32 for ultra-fast hashing
char *create_mixed_ascii_frame_for_client(const char *target_client_id, unsigned short width, unsigned short height,
                                          bool wants_stretch, size_t *out_size, bool *out_grid_changed,
                                          int *out_sources_count) {
  (void)wants_stretch; // Unused - we always handle aspect ratio ourselves

  uint64_t frame_gen_start_ns = time_get_ns();

  // Initialize output parameters
  if (out_grid_changed) {
    *out_grid_changed = false;
  }
  if (out_sources_count) {
    *out_sources_count = 0;
  }

  if (!out_size || width == 0 || height == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "Invalid parameters for create_mixed_ascii_frame_for_client: width=%u, height=%u, out_size=%p", width,
              height, out_size);
    return NULL;
  }

  // Collect all active clients and their image sources
  image_source_t sources[MAX_CLIENTS];
  uint64_t collect_start_ns = time_get_ns();
  int source_count = collect_video_sources(sources, MAX_CLIENTS);
  uint64_t collect_end_ns = time_get_ns();

  // Count sources that actually have video data
  int sources_with_video = 0;
  for (int i = 0; i < source_count; i++) {
    if (sources[i].has_video && sources[i].image) {
      sources_with_video++;
    }
  }

  static uint64_t last_detailed_log = 0;
  uint64_t now_ns = collect_end_ns;
  if (now_ns - last_detailed_log > 333 * NS_PER_MS_INT) { // Log every 333ms (3x per second)
    last_detailed_log = now_ns;
    log_info("FRAME_GEN_START: target_client=%u sources=%d collect=%.1fms", target_client_id, sources_with_video,
             (collect_end_ns - collect_start_ns) / NS_PER_MS);
  }

  // Return the source count for debugging/tracking
  if (out_sources_count) {
    *out_sources_count = sources_with_video;
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
      log_dev_every(
          LOG_RATE_DEFAULT,
          "Grid layout changing: %d -> %d active video sources - caller will broadcast clear AFTER buffering frame",
          previous_count, sources_with_video);
      if (out_grid_changed) {
        *out_grid_changed = true; // Signal to caller
      }
    }
  }

  // No active video sources - don't generate placeholder frames
  image_t *composite = NULL;

  if (sources_with_video == 0) {
    *out_size = 0;
    // No active video sources for client - this isn't an error.
    // Return NULL to indicate no frame should be sent
    return NULL;
  }

  if (sources_with_video == 1) {
    // Single source handling - create composite and convert to ASCII
    // Note: create_single_source_composite returns a reference to sources[i].image
    // which could be modified by other threads. Make a copy to prevent concurrent
    // modification during ascii_convert_with_capabilities.
    image_t *single_source = create_single_source_composite(sources, source_count, target_client_id, width, height);
    if (single_source) {
      composite = image_new_copy(single_source);
      if (!composite) {
        SET_ERRNO(ERROR_MEMORY, "Failed to copy single source composite");
        *out_size = 0;
        return NULL;
      }
    }
  } else {
    // Multiple sources - create grid layout
    composite =
        create_multi_source_composite(sources, source_count, sources_with_video, target_client_id, width, height);
  }

  char *out = NULL;

  if (!composite) {
    SET_ERRNO(ERROR_INVALID_STATE, "Per-client %s: Failed to create composite image", target_client_id);
    *out_size = 0;
    out = NULL;
  }

  // Convert composite to ASCII using client capabilities
  // Pass terminal dimensions so the frame can be padded to full width
  char *ascii_frame = convert_composite_to_ascii(composite, target_client_id, width, height);

  if (ascii_frame) {
    // The frame should have been null-terminated by the padding functions.
    // Use strlen() which is optimized and reliable
    size_t ascii_len = strlen(ascii_frame);

    // Safety check: don't accept unreasonably large frames (10MB limit)
    if (ascii_len > 10 * 1024 * 1024) {
      log_error("Frame size exceeds 10MB safety limit (possible buffer overflow)");
      SET_ERRNO(ERROR_INVALID_PARAM, "Frame size exceeds 10MB");
      return NULL;
    }

    // Ensure frame ends with a reset sequence to avoid garbage at terminal
    // This prevents color codes from leaking into uninitialized memory
    const char reset_seq[] = "\033[0m";
    const size_t reset_len = 4;

    if (ascii_len >= reset_len) {
      // Check if frame already ends with reset
      const char *frame_end = ascii_frame + ascii_len - reset_len;
      if (strncmp(frame_end, reset_seq, reset_len) == 0) {
        // Frame properly ends with reset, use full length
        *out_size = ascii_len;
      } else {
        // Frame doesn't end with reset - this is the REAL bug!
        // Truncate to the last occurrence of reset sequence
        const char *last_reset = NULL;
        for (const char *p = ascii_frame + ascii_len - reset_len; p >= ascii_frame; p--) {
          if (strncmp(p, reset_seq, reset_len) == 0) {
            last_reset = p;
            break;
          }
        }

        if (last_reset) {
          // Include the reset sequence and truncate after it
          *out_size = (size_t)(last_reset - ascii_frame) + reset_len;
          ascii_frame[*out_size] = '\0'; // Ensure null termination
          log_warn("Frame was missing reset at end (had garbage), truncated from %zu to %zu bytes", ascii_len,
                   *out_size);
        } else {
          // No reset found, use full length as fallback
          *out_size = ascii_len;
          log_warn("Frame has no reset sequences, sending full %zu bytes", ascii_len);
        }
      }
    } else {
      // Frame too short to have a reset, use as-is
      *out_size = ascii_len;
    }

    log_dev_every(LOG_RATE_SLOW, "create_mixed_ascii_frame_for_client: Final frame size=%zu bytes for client %u",
                  *out_size, target_client_id);

    // Debug: Log the last 50 bytes of the frame to see what's really there
    if (*out_size >= 50) {
      char hex_buf[300] = {0};
      size_t hex_len = 0;
      const uint8_t *last_bytes = (const uint8_t *)ascii_frame + (*out_size - 50);
      for (int i = 0; i < 50 && hex_len < sizeof(hex_buf) - 5; i++) {
        hex_len += snprintf(hex_buf + hex_len, sizeof(hex_buf) - hex_len, "%02X ", last_bytes[i]);
      }
      log_dev_every(4500 * US_PER_MS_INT, "FRAME_LAST_50_BYTES (hex): %s", hex_buf);

      // Also log as ASCII for readability
      char ascii_buf[100] = {0};
      for (int i = 0; i < 50 && i < (int)sizeof(ascii_buf) - 1; i++) {
        if (last_bytes[i] >= 32 && last_bytes[i] < 127) {
          ascii_buf[i] = (char)last_bytes[i];
        } else if (last_bytes[i] == '\n') {
          ascii_buf[i] = 'N';
        } else if (last_bytes[i] == '\0') {
          ascii_buf[i] = '0';
        } else {
          ascii_buf[i] = '.';
        }
      }
      log_dev_every(4500 * US_PER_MS_INT, "FRAME_LAST_50_ASCII: %s", ascii_buf);
    }

    out = ascii_frame;
  } else {
    SET_ERRNO(ERROR_TERMINAL, "Per-client %u: Failed to convert image to ASCII", target_client_id);
    *out_size = 0;
  }

  if (composite) {
    // For single source, composite is a malloc-allocated copy, not from pool
    // Check alloc method to determine correct destroy function
    if (composite->alloc_method == IMAGE_ALLOC_POOL) {
      image_destroy_to_pool(composite);
    } else {
      image_destroy(composite);
    }
  }
  for (int i = 0; i < source_count; i++) {
    if (sources[i].image) {
      image_destroy_to_pool(sources[i].image);
    }
  }

  uint64_t frame_gen_end_ns = time_get_ns();
  uint64_t frame_gen_duration_ns = frame_gen_end_ns - frame_gen_start_ns;
  if (frame_gen_duration_ns > 10 * NS_PER_MS_INT) { // Log if > 10ms
    char duration_str[32];
    time_pretty((uint64_t)((double)frame_gen_duration_ns), -1, duration_str, sizeof(duration_str));
    log_warn("SLOW_FRAME_GENERATION: Client %u full frame gen took %s", target_client_id, duration_str);
  }

  return out;
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
 * - Uses PACKET_TYPE_AUDIO_BATCH packet type
 * - Audio data is raw float samples bundled together
 * - Batch format reduces packet overhead ~32x
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

  return packet_queue_enqueue(client->audio_queue, PACKET_TYPE_AUDIO_BATCH, audio_data, data_size, 0, true);
}

/**
 * @brief Check if any connected clients are currently sending video
 *
 * This function scans all active clients to determine if at least one is
 * sending video frames. Used by render threads to avoid generating frames
 * when no video sources are available (e.g., during webcam warmup).
 *
 * LOCK OPTIMIZATION: Uses atomic reads only, no rwlock acquisition
 * This is safe because client_id, active, and is_sending_video are all atomics
 *
 * @return true if at least one client has is_sending_video flag set, false otherwise
 */
bool any_clients_sending_video(void) {
  // LOCK OPTIMIZATION: Don't acquire rwlock - all fields we access are atomic
  // client_id, active, is_sending_video are all atomic variables

  // Iterate through all client slots
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

    // Skip uninitialized clients (atomic read)
    if (client->client_id[0] == '\0') {
      continue;
    }

    // Check if client is active and sending video (both atomic reads)
    bool is_active = atomic_load(&client->active);
    bool is_sending = atomic_load(&client->is_sending_video);

    if (is_active && is_sending) {
      return true;
    }
  }

  return false;
}
