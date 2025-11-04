
/**
 * @file os/webcam.c
 * @ingroup webcam
 * @brief 📷 Cross-platform webcam abstraction layer for Media Foundation (Windows) and V4L2 (Linux)
 */

#include <stdio.h>

#include "webcam.h"
#include "common.h"
#include "options.h"
#include "image2ascii/image.h"
#include "platform/string.h"

static webcam_context_t *global_webcam_ctx = NULL;

asciichat_error_t webcam_init(unsigned short int webcam_index) {
  // Check if test pattern mode is enabled
  if (opt_test_pattern) {
    log_info("Test pattern mode enabled - not opening real webcam");
    log_info("Test pattern resolution: 1280x720");
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
  if (opt_test_pattern) {
    // Generate a test pattern image
    static int frame_counter = 0;
    frame_counter++;

    // Create a new image with standard webcam dimensions (1280x720)
    image_t *test_frame = image_new(1280, 720);
    if (!test_frame) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate test pattern frame");
      return NULL;
    }

    // Generate a colorful test pattern with moving elements
    for (int y = 0; y < test_frame->h; y++) {
      for (int x = 0; x < test_frame->w; x++) {
        rgb_t *pixel = &test_frame->pixels[y * test_frame->w + x];

        // Create a grid pattern with color bars and animated elements
        int grid_x = x / 160; // 8 vertical sections
        // int grid_y = y / 120;  // 6 horizontal sections (unused for now)

        // Base pattern: color bars
        switch (grid_x) {
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
          pixel->r = 0;
          pixel->g = 0;
          pixel->b = 255;
          break;
        case 3: // Yellow
          pixel->r = 255;
          pixel->g = 255;
          pixel->b = 0;
          break;
        case 4: // Cyan
          pixel->r = 0;
          pixel->g = 255;
          pixel->b = 255;
          break;
        case 5: // Magenta
          pixel->r = 255;
          pixel->g = 0;
          pixel->b = 255;
          break;
        case 6: // White
          pixel->r = 255;
          pixel->g = 255;
          pixel->b = 255;
          break;
        case 7: // Gray gradient
        default: {
          uint8_t gray = (uint8_t)((y * 255) / test_frame->h);
          pixel->r = gray;
          pixel->g = gray;
          pixel->b = gray;
          break;
        }
        }

        // Add a moving diagonal pattern
        int diagonal = (x + y + frame_counter * 10) % 256;
        pixel->r = (uint8_t)((int)pixel->r + diagonal) / 2;
        pixel->g = (uint8_t)((int)pixel->g + diagonal) / 2;
        pixel->b = (uint8_t)((int)pixel->b + diagonal) / 2;

        // Add grid lines for visual separation (but skip center lines to avoid artifacts)
        int center_x = test_frame->w / 2;
        int center_y = test_frame->h / 2;
        bool is_center_line = (x == center_x || y == center_y);
        if (!is_center_line && (x % 160 == 0 || y % 120 == 0)) {
          pixel->r = 0;
          pixel->g = 0;
          pixel->b = 0;
        }
      }
    }

    // Note: Center crosshair removed to avoid visual artifacts in ASCII output
    // The bright white line was creating a horizontal 'M' stripe across the display

    // Apply horizontal flip if requested (same as real webcam)
    if (opt_webcam_flip) {
      for (int y = 0; y < test_frame->h; y++) {
        for (int x = 0; x < test_frame->w / 2; x++) {
          rgb_t temp = test_frame->pixels[y * test_frame->w + x];
          test_frame->pixels[y * test_frame->w + x] = test_frame->pixels[y * test_frame->w + (test_frame->w - 1 - x)];
          test_frame->pixels[y * test_frame->w + (test_frame->w - 1 - x)] = temp;
        }
      }
    }

    return test_frame;
  }

  if (!global_webcam_ctx) {
    SET_ERRNO(ERROR_WEBCAM, "Webcam not initialized - global_webcam_ctx is NULL");
    return NULL;
  }

  image_t *frame = webcam_read_context(global_webcam_ctx);

  if (!frame) {
    return NULL;
  }

  // Apply horizontal flip if requested
  if (opt_webcam_flip) {
    // Flip the image horizontally - optimized for large images
    // Process entire rows to improve cache locality
    rgb_t *left = frame->pixels;
    rgb_t *right = frame->pixels + frame->w - 1;

    for (int y = 0; y < frame->h; y++) {
      rgb_t *row_left = left;
      rgb_t *row_right = right;

      // Swap pixels from both ends moving inward
      for (int x = 0; x < frame->w / 2; x++) {
        rgb_t temp = *row_left;
        *row_left++ = *row_right;
        *row_right-- = temp;
      }

      // Move to next row
      left += frame->w;
      right += frame->w;
    }
  }

  return frame;
}

void webcam_cleanup(void) {
  if (opt_test_pattern) {
    log_info("Test pattern mode - no webcam resources to release");
    return;
  }

  if (global_webcam_ctx) {
    webcam_cleanup_context(global_webcam_ctx);
    global_webcam_ctx = NULL;
    // log_info("Webcam resources released");
  } else {
    log_info("Webcam was not opened, nothing to release");
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
#endif
