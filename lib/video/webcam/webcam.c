
/**
 * @file os/webcam.c
 * @ingroup webcam
 * @brief 📷 Cross-platform webcam abstraction layer for Media Foundation (Windows) and V4L2 (Linux)
 */

#include <stdio.h>

#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/platform/string.h>

static webcam_context_t *global_webcam_ctx = NULL;
static image_t *cached_webcam_frame = NULL;
static unsigned int frame_counter = 0; // Counter for animated test pattern

asciichat_error_t webcam_init(unsigned short int webcam_index) {
  // Check if test pattern mode is enabled
  if (GET_OPTION(test_pattern)) {
    log_info("Test pattern mode enabled - not opening real webcam");
    log_info("Test pattern resolution: 320x240");
    return ASCIICHAT_OK;
  }

#ifdef __linux__
  log_info("Initializing webcam with V4L2 (Linux)");
  log_info("Attempting to open webcam with index %d using V4L2 (Linux)...", webcam_index);
#elif defined(__APPLE__)
  log_info("Initializing webcam with AVFoundation (macOS)");
  log_info("Attempting to open webcam with index %d using AVFoundation (macOS)...", webcam_index);
#elif defined(_WIN32)
  log_info("Initializing webcam with Media Foundation (Windows)");
  log_info("Attempting to open webcam with index %d using Media Foundation (Windows)...", webcam_index);
#else
  log_info("Initializing webcam with Unknown platform");
  log_info("Attempting to open webcam with index %d using Unknown platform...", webcam_index);
#endif

  asciichat_error_t result = webcam_init_context(&global_webcam_ctx, webcam_index);
  if (result != ASCIICHAT_OK) {
    SET_ERRNO(result, "Failed to connect to webcam (error code: %d)", result);
    return result;
  }

  // Get image dimensions
  int width, height;
  if (webcam_get_dimensions(global_webcam_ctx, &width, &height) == ASCIICHAT_OK) {
    log_info("Webcam opened successfully! Resolution: %dx%d", width, height);
  } else {
    SET_ERRNO(ERROR_WEBCAM, "Webcam opened but failed to get dimensions");
  }

  return result;
}

image_t *webcam_read(void) {
  // Check if test pattern mode is enabled
  if (GET_OPTION(test_pattern)) {
    // Allocate cached frame once (like FFmpeg's current_image)
    // Reuse same buffer for each call to avoid repeated allocations
    if (!cached_webcam_frame) {
      cached_webcam_frame = image_new(320, 240);
      if (!cached_webcam_frame) {
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate test pattern frame");
        return NULL;
      }
    }

    // Generate animated test pattern each frame
    // Animation is based on frame counter, respects FPS setting
    unsigned int animation_phase = frame_counter / 2; // Slow down animation
    frame_counter++;

    for (int y = 0; y < cached_webcam_frame->h; y++) {
      for (int x = 0; x < cached_webcam_frame->w; x++) {
        rgb_pixel_t *pixel = &cached_webcam_frame->pixels[y * cached_webcam_frame->w + x];

        // Animated color bars that shift based on frame counter
        int animated_x = (x + animation_phase) % cached_webcam_frame->w;
        int grid_x = animated_x / 40;

        // Base pattern: color bars that animate horizontally
        switch (grid_x % 3) {
        case 0: // Red
          pixel->r = 255;
          pixel->g = 0;
          pixel->b = 0;
          break;
        case 1: // Green
          pixel->r = 0;
          pixel->g = 255;
          pixel->b = 0;
          break;
        case 2: // Blue
        default:
          pixel->r = 0;
          pixel->g = 0;
          pixel->b = 255;
          break;
        }

        // Add animated grid lines
        if (animated_x % 40 == 0 || y % 30 == 0) {
          pixel->r = 0;
          pixel->g = 0;
          pixel->b = 0;
        }
      }
    }

    return cached_webcam_frame;
  }

  if (!global_webcam_ctx) {
    SET_ERRNO(ERROR_WEBCAM, "Webcam not initialized - global_webcam_ctx is NULL");
    return NULL;
  }

  image_t *frame = webcam_read_context(global_webcam_ctx);

  if (!frame) {
    return NULL;
  }

  return frame;
}

void webcam_destroy(void) {
  // Free cached webcam frame if it was allocated (from test pattern or real webcam)
  if (cached_webcam_frame) {
    image_destroy(cached_webcam_frame);
    cached_webcam_frame = NULL;
  }

  if (GET_OPTION(test_pattern)) {
    log_debug("Test pattern mode - webcam context cleanup skipped");
    return;
  }

  if (global_webcam_ctx) {
    webcam_cleanup_context(global_webcam_ctx);
    global_webcam_ctx = NULL;
    // log_info("Webcam resources released");
  } else {
    log_dev("Webcam was not opened, nothing to release");
  }
}

void webcam_flush(void) {
  if (GET_OPTION(test_pattern)) {
    return; // Test pattern doesn't need flushing
  }

  if (global_webcam_ctx) {
    webcam_flush_context(global_webcam_ctx);
  }
}

void webcam_print_init_error_help(asciichat_error_t error_code) {
  // Platform-specific error messages and troubleshooting help
#ifdef __linux__
  safe_fprintf(stderr, "\n");

  if (error_code == ERROR_WEBCAM) {
    safe_fprintf(stderr, "Webcam initialization failed on Linux.\n\n");
    safe_fprintf(stderr, "Common solutions:\n");
    safe_fprintf(stderr, "  1. Check if a camera is connected:\n");
    safe_fprintf(stderr, "       ls /dev/video*\n\n");
    safe_fprintf(stderr, "  2. If no camera is available, use test pattern mode:\n");
    safe_fprintf(stderr, "       ascii-chat client --test-pattern\n\n");
    safe_fprintf(stderr, "  3. Install V4L2 drivers if needed:\n");
    safe_fprintf(stderr, "       sudo apt-get install v4l-utils\n");
  } else if (error_code == ERROR_WEBCAM_PERMISSION) {
    safe_fprintf(stderr, "Camera permission denied.\n\n");
    safe_fprintf(stderr, "Fix permissions with:\n");
    safe_fprintf(stderr, "  sudo usermod -a -G video $USER\n");
    safe_fprintf(stderr, "Then log out and log back in for changes to take effect.\n");
  } else if (error_code == ERROR_WEBCAM_IN_USE) {
    safe_fprintf(stderr, "Camera is already in use by another application.\n\n");
    safe_fprintf(stderr, "Try closing other camera apps or use test pattern mode:\n");
    safe_fprintf(stderr, "  ascii-chat client --test-pattern\n");
  } else {
    safe_fprintf(stderr, "Webcam error on Linux.\n\n");
    safe_fprintf(stderr, "General troubleshooting:\n");
    safe_fprintf(stderr, "* Check camera: ls /dev/video*\n");
    safe_fprintf(stderr, "* Check permissions: groups | grep video\n");
    safe_fprintf(stderr, "* Use test pattern: ascii-chat client --test-pattern\n");
  }
  (void)fflush(stderr);
#elif defined(__APPLE__)
  (void)error_code;
  safe_fprintf(stderr, "\n");
  safe_fprintf(stderr, "On macOS, you may need to grant camera permissions:\n");
  safe_fprintf(stderr,
               "* Say \"yes\" to the popup about system camera access that you see when running this program for the "
               "first time.\n");
  safe_fprintf(
      stderr, "* If you said \"no\" to the popup, go to System Preferences > Security & Privacy > Privacy > Camera.\n");
  safe_fprintf(stderr,
               "   Now flip the switch next to your terminal application in that privacy list to allow ascii-chat to "
               "access your camera.\n");
  safe_fprintf(stderr, "   Then just run this program again.\n");
  (void)fflush(stderr);
#elif defined(_WIN32)
  if (error_code == ERROR_WEBCAM_IN_USE) {
    // Device is in use by another application - this is a fatal error on Windows
    safe_fprintf(stderr, "\n");
    safe_fprintf(stderr, "Webcam is already in use by another application.\n");
    safe_fprintf(stderr, "Windows allows only one application to access the webcam at a time.\n");
    safe_fprintf(stderr, "\n");
    safe_fprintf(stderr, "To use ascii-chat with multiple clients, try these alternatives:\n");
    safe_fprintf(stderr, "  --test-pattern    Generate a colorful test pattern instead of using webcam\n");
    safe_fprintf(stderr, "  --file VIDEO.mp4  Use a video file as input (to be implemented)\n");
    safe_fprintf(stderr, "\n");
    safe_fprintf(stderr, "Example: ascii-chat client --test-pattern\n");
    (void)fflush(stderr);
  } else {
    // Other webcam errors - general failure
    safe_fprintf(stderr, "\n");
    safe_fprintf(stderr, "On Windows, this might be because:\n");
    safe_fprintf(stderr, "* Camera permissions are not granted\n");
    safe_fprintf(stderr, "* Camera driver issues\n");
    safe_fprintf(stderr, "* No webcam device found\n");
    (void)fflush(stderr);
  }
#else
  // Unknown platform
  (void)error_code;
  safe_fprintf(stderr, "\nWebcam initialization failed on unsupported platform.\n");
  (void)fflush(stderr);
#endif
}

// Fallback implementations for unsupported platforms
#if !defined(__linux__) && !defined(__APPLE__) && !defined(_WIN32)
asciichat_error_t webcam_init_context(webcam_context_t **ctx, unsigned short int device_index) {
  (void)ctx;
  (void)device_index;
  SET_ERRNO(ERROR_WEBCAM, "Webcam platform not supported on this system");
  return ERROR_WEBCAM;
}

void webcam_cleanup_context(webcam_context_t *ctx) {
  (void)ctx;
  log_warn("Webcam cleanup called on unsupported platform");
}

void webcam_flush_context(webcam_context_t *ctx) {
  (void)ctx;
  // No-op on unsupported platforms
}

image_t *webcam_read_context(webcam_context_t *ctx) {
  (void)ctx;
  SET_ERRNO(ERROR_WEBCAM, "Webcam read not supported on this platform");
  return NULL;
}

asciichat_error_t webcam_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
  (void)ctx;
  (void)width;
  (void)height;
  return SET_ERRNO(ERROR_WEBCAM, "Webcam get dimensions not supported on this platform");
}

asciichat_error_t webcam_list_devices(webcam_device_info_t **out_devices, unsigned int *out_count) {
  if (out_devices)
    *out_devices = NULL;
  if (out_count)
    *out_count = 0;
  return SET_ERRNO(ERROR_WEBCAM, "Webcam device enumeration not supported on this platform");
}

void webcam_free_device_list(webcam_device_info_t *devices) {
  (void)devices;
  // No-op on unsupported platforms
}
#endif
