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
#include <math.h>
#include <float.h>

#include "stream.h"
#include "client.h"
#include "common.h"
#include "buffer_pool.h"
#include "packet_queue.h"
#include "ringbuffer.h"
#include "video_frame.h"
#include "image2ascii/image.h"
#include "image2ascii/ascii.h"
#include "util/aspect_ratio.h"

// Global client manager from client.c - needed for any_clients_sending_video()
extern rwlock_t g_client_manager_rwlock;
extern client_manager_t g_client_manager;

// Track previous active video source count for grid layout change detection
static atomic_int g_previous_active_video_count = 0;

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

// Helper function to clean up current_frame.data using the appropriate method
static void cleanup_current_frame_data(multi_source_frame_t *frame) {
  if (frame && frame->data) {
    data_buffer_pool_t *pool = data_buffer_pool_get_global();
    if (pool) {
      data_buffer_pool_free(pool, frame->data, frame->size);
    } else {
      SAFE_FREE(frame->data);
    }
    frame->data = NULL;
  }
}

// Structure for image sources
typedef struct {
  image_t *image;
  uint32_t client_id;
  bool has_video; // Whether this client has video or is just a placeholder
} image_source_t;

// Collect video frames from all active clients
static int collect_video_sources(image_source_t *sources, int max_sources) {
  int source_count = 0;

  // Check for shutdown before acquiring locks to prevent lock corruption
  if (atomic_load(&g_server_should_exit)) {
    return 0;
  }

  // LOCK OPTIMIZATION: No locks needed! All client fields are atomic or stable pointers
  // client_id, active, is_sending_video are all atomic variables
  // incoming_video_buffer is set once during client creation and never changes

  // Collect client info snapshots WITHOUT holding rwlock
  typedef struct {
    uint32_t client_id;
    bool is_active;
    bool is_sending_video;
    video_frame_buffer_t *video_buffer;
  } client_snapshot_t;

  client_snapshot_t client_snapshots[MAX_CLIENTS];
  int snapshot_count = 0;

  // NO LOCK: All fields are atomic or stable pointers
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];

    if (atomic_load(&client->client_id) == 0) {
      continue; // Skip uninitialized clients
    }

    // Snapshot all needed client state (all atomic reads or stable pointers)
    client_snapshots[snapshot_count].client_id = atomic_load(&client->client_id);
    client_snapshots[snapshot_count].is_active = atomic_load(&client->active);
    client_snapshots[snapshot_count].is_sending_video = atomic_load(&client->is_sending_video);
    client_snapshots[snapshot_count].video_buffer = client->incoming_video_buffer; // Stable pointer
    snapshot_count++;
  }

  // Process frames (expensive operations)
  for (int i = 0; i < snapshot_count && source_count < max_sources; i++) {
    client_snapshot_t *snap = &client_snapshots[i];

    if (!snap->is_active) {
      continue;
    }

    sources[source_count].client_id = snap->client_id;
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

      if (frame_data_ptr && frame_size_val > 0) {
        // We have frame data - copy it to our working structure
        data_buffer_pool_t *pool = data_buffer_pool_get_global();
        if (pool) {
          current_frame.data = data_buffer_pool_alloc(pool, frame->size);
        }
        if (!current_frame.data) {
          current_frame.data = SAFE_MALLOC(frame->size, void *);
        }

        if (current_frame.data) {
          memcpy(current_frame.data, frame->data, frame->size);
          current_frame.size = frame->size;
          current_frame.source_client_id = snap->client_id;
          current_frame.timestamp = (uint32_t)(frame->capture_timestamp_us / 1000000);
          got_new_frame = true;
        }
      } else {
      }
    } else {
    }

    multi_source_frame_t *frame_to_use = got_new_frame ? &current_frame : NULL;

    if (frame_to_use && frame_to_use->data && frame_to_use->size > sizeof(uint32_t) * 2) {
      // Parse the image data
      // Format: [width:4][height:4][rgb_data:w*h*3]
      uint32_t img_width = ntohl(*(uint32_t *)frame_to_use->data);
      uint32_t img_height = ntohl(*(uint32_t *)(frame_to_use->data + sizeof(uint32_t)));

      // Debug logging to understand the data
      if (img_width == 0xBEBEBEBE || img_height == 0xBEBEBEBE) {
        SET_ERRNO(ERROR_INVALID_STATE, "UNINITIALIZED MEMORY DETECTED! First 16 bytes of frame data:");
        uint8_t *bytes = (uint8_t *)frame_to_use->data;
        SET_ERRNO(ERROR_INVALID_STATE,
                  "  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", bytes[0],
                  bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
                  bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
      }

      // Validate dimensions are reasonable (max 4K resolution)
      if (img_width == 0 || img_width > 4096 || img_height == 0 || img_height > 4096) {
        SET_ERRNO(ERROR_INVALID_STATE,
                  "Per-client: Invalid image dimensions from client %u: %ux%u (data may be corrupted)", snap->client_id,
                  img_width, img_height);
        // Clean up the current frame if we got a new one
        if (got_new_frame) {
          cleanup_current_frame_data(&current_frame);
        }
        source_count++;
        continue;
      }

      // Validate that the frame size matches expected size
      size_t expected_size = sizeof(uint32_t) * 2 + (size_t)img_width * (size_t)img_height * sizeof(rgb_t);
      if (frame_to_use->size != expected_size) {
        SET_ERRNO(ERROR_INVALID_STATE,
                  "Per-client: Frame size mismatch from client %u: got %zu, expected %zu for %ux%u image",
                  snap->client_id, frame_to_use->size, expected_size, img_width, img_height);
        // Clean up the current frame if we got a new one
        if (got_new_frame) {
          cleanup_current_frame_data(&current_frame);
        }
        source_count++;
        continue;
      }

      // Extract pixel data
      rgb_t *pixels = (rgb_t *)(frame_to_use->data + (sizeof(uint32_t) * 2));

      // Create image from buffer pool for consistent video pipeline management
      image_t *img = image_new_from_pool(img_width, img_height);
      memcpy(img->pixels, pixels, (size_t)img_width * (size_t)img_height * sizeof(rgb_t));
      sources[source_count].image = img;
      sources[source_count].has_video = true;
    }

    // Clean up current_frame.data if we allocated it but frame_to_use check failed
    // This handles cases where: frame too small, no data, etc.
    if (got_new_frame && current_frame.data) {
      cleanup_current_frame_data(&current_frame);
    }

    // Increment source count for this active client (with or without video)
    source_count++;
  }

  return source_count;
}

// Handle single source video layout
static image_t *create_single_source_composite(image_source_t *sources, int source_count, uint32_t target_client_id,
                                               unsigned short width, unsigned short height) {
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

  // Single source - check if target client wants half-block mode for 2x resolution
  // LOCK OPTIMIZATION: Find client without calling find_client_by_id() to avoid rwlock
  client_info_t *target_client = NULL;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (atomic_load(&client->client_id) == target_client_id) {
      target_client = client;
      break;
    }
  }
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
  image_t *composite = image_new_from_pool(composite_width_px, composite_height_px);
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
  } else {
    // Normal modes: Simple resize to fitted dimensions
    image_resize(single_source, composite);
  }

  return composite;
}

/**
 * Calculate optimal grid layout that maximizes space usage
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
    log_debug("  Testing %dx%d: cell=%dx%d (visual aspect %.2f), utilization=%.1f%%", cols, rows, cell_width,
              cell_height, test_cell_visual_aspect, utilization * 100.0f);

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
  log_info("Grid layout: %d clients -> %dx%d grid (%.1f%% utilization) | terminal=%dx%d (char aspect %.2f, VISUAL "
           "aspect %.2f), video aspect: %.2f",
           sources_with_video, best_cols, best_rows, best_utilization * 100.0f, terminal_width, terminal_height,
           (float)terminal_width / (float)terminal_height, terminal_visual_aspect, avg_aspect);
}

// Handle multi-source grid layout
static image_t *create_multi_source_composite(image_source_t *sources, int source_count, int sources_with_video,
                                              uint32_t target_client_id, unsigned short width, unsigned short height) {
  (void)target_client_id; // Unused - composite is same for all clients now

  // Calculate optimal grid layout using space-maximizing algorithm
  int grid_cols, grid_rows;
  calculate_optimal_grid_layout(sources, source_count, sources_with_video, width, height, &grid_cols, &grid_rows);

  // Calculate composite dimensions in PIXELS for half-block mode
  // Terminal dimensions are in CHARACTERS, need to convert to pixels:
  // - Width: 1 char = 1 horizontal pixel
  // - Height: 1 char = 2 vertical pixels (half-block = 2 pixels per char)
  const int PIXELS_PER_CHAR_HEIGHT = 2;
  int composite_width_px = width;                              // chars = pixels horizontally
  int composite_height_px = height * PIXELS_PER_CHAR_HEIGHT;   // chars * 2 = pixels vertically

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

    log_info("Cell %d: %dx%d px, video %.2f, cell %.2f → target %dx%d px (fill %s)",
             video_source_index - 1, cell_width_px, cell_height_px, src_aspect,
             cell_visual_aspect, target_width_px, target_height_px,
             (src_aspect > cell_visual_aspect) ? "WIDTH" : "HEIGHT");

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
          continue;  // Skip pixels outside cell boundaries
        }

        // Additional composite boundary check
        if (dst_x < 0 || dst_x >= composite->w || dst_y < 0 || dst_y >= composite->h) {
          continue;  // Skip pixels outside composite
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

// Convert composite image to ASCII using client capabilities
// OPTIMIZATION: Takes client_id instead of finding client, avoids extra rwlock acquisition
static char *convert_composite_to_ascii(image_t *composite, uint32_t target_client_id, unsigned short width,
                                        unsigned short height) {
  // LOCK OPTIMIZATION: Don't call find_client_by_id() - it would acquire rwlock unnecessarily
  // Instead, the render thread already has snapshot of client state, so we just need palette data
  // which is stable after initialization

  // We need to find the client to access palette data, but we can do this without locking
  // since palette is initialized once and never changes
  client_info_t *render_client = NULL;

  // Find client without locking - client_id is atomic and stable once set
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (atomic_load(&client->client_id) == target_client_id) {
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
  char *ascii_frame =
      ascii_convert_with_capabilities(composite, width, h, &caps_snapshot, true, false,
                                      render_client->client_palette_chars, render_client->client_luminance_palette);

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
char *create_mixed_ascii_frame_for_client(uint32_t target_client_id, unsigned short width, unsigned short height,
                                          bool wants_stretch, size_t *out_size, bool *out_grid_changed,
                                          int *out_sources_count) {
  (void)wants_stretch; // Unused - we always handle aspect ratio ourselves

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

  // PROFILING: Time collection phase
  struct timespec prof_collect_start, prof_collect_end;
  (void)clock_gettime(CLOCK_MONOTONIC, &prof_collect_start);

  // Collect all active clients and their image sources
  image_source_t sources[MAX_CLIENTS];
  int source_count = collect_video_sources(sources, MAX_CLIENTS);

  (void)clock_gettime(CLOCK_MONOTONIC, &prof_collect_end);
  uint64_t collect_time_us =
      ((uint64_t)prof_collect_end.tv_sec * 1000000 + (uint64_t)prof_collect_end.tv_nsec / 1000) -
      ((uint64_t)prof_collect_start.tv_sec * 1000000 + (uint64_t)prof_collect_start.tv_nsec / 1000);

  // Count sources that actually have video data
  int sources_with_video = 0;
  for (int i = 0; i < source_count; i++) {
    if (sources[i].has_video && sources[i].image) {
      sources_with_video++;
    }
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
      log_info(
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

  // PROFILING: Time composite generation
  struct timespec prof_composite_start, prof_composite_end;
  (void)clock_gettime(CLOCK_MONOTONIC, &prof_composite_start);

  if (sources_with_video == 1) {
    // Single source handling
    composite = create_single_source_composite(sources, source_count, target_client_id, width, height);
  } else {
    // Multiple sources - create grid layout
    composite =
        create_multi_source_composite(sources, source_count, sources_with_video, target_client_id, width, height);
  }

  (void)clock_gettime(CLOCK_MONOTONIC, &prof_composite_end);
  uint64_t composite_time_us =
      ((uint64_t)prof_composite_end.tv_sec * 1000000 + (uint64_t)prof_composite_end.tv_nsec / 1000) -
      ((uint64_t)prof_composite_start.tv_sec * 1000000 + (uint64_t)prof_composite_start.tv_nsec / 1000);

  if (!composite) {
    SET_ERRNO(ERROR_INVALID_STATE, "Per-client %u: Failed to create composite image", target_client_id);
    *out_size = 0;
    goto cleanup;
  }

  // PROFILING: Time ASCII conversion
  struct timespec prof_ascii_start, prof_ascii_end;
  (void)clock_gettime(CLOCK_MONOTONIC, &prof_ascii_start);

  // Convert composite to ASCII using client capabilities
  char *ascii_frame = convert_composite_to_ascii(composite, target_client_id, width, height);

  (void)clock_gettime(CLOCK_MONOTONIC, &prof_ascii_end);
  uint64_t ascii_time_us = ((uint64_t)prof_ascii_end.tv_sec * 1000000 + (uint64_t)prof_ascii_end.tv_nsec / 1000) -
                           ((uint64_t)prof_ascii_start.tv_sec * 1000000 + (uint64_t)prof_ascii_start.tv_nsec / 1000);
  (void)collect_time_us;
  (void)composite_time_us;
  (void)ascii_time_us;

  if (ascii_frame) {
    *out_size = strlen(ascii_frame);
  } else {
    SET_ERRNO(ERROR_TERMINAL, "Per-client %u: Failed to convert image to ASCII", target_client_id);
    *out_size = 0;
    goto cleanup;
  }

cleanup:
  if (composite) {
    image_destroy_to_pool(composite);
  }
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
    if (atomic_load(&client->client_id) == 0) {
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
