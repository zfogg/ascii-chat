#include <stdio.h>
#include <stdlib.h>

#include "webcam.h"
#include "common.h"
#include "options.h"
#include "image2ascii/image.h"

static webcam_context_t *global_webcam_ctx = NULL;

int webcam_init(unsigned short int webcam_index) {
  // Check if test pattern mode is enabled
  if (opt_test_pattern) {
    log_info("Test pattern mode enabled - not opening real webcam");
    // Set standard webcam dimensions for test pattern
    last_image_width = 1280;
    last_image_height = 720;
    log_info("Test pattern resolution: %dx%d", last_image_width, last_image_height);
    return 0;
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

  int result = webcam_init_context(&global_webcam_ctx, webcam_index);
  if (result != 0) {
    log_error("Failed to connect to webcam");

    // Platform-specific error messages
#ifdef __linux__
    fprintf(stderr, "\n");
    fprintf(stderr, "On Linux, make sure:\n");
    fprintf(stderr, "* Your user is in the 'video' group: sudo usermod -a -G video $USER\n");
    fprintf(stderr, "* The camera device exists: ls /dev/video*\n");
    fprintf(stderr, "* No other application is using the camera\n");
    fflush(stderr);
    exit(ASCIICHAT_ERR_WEBCAM);
#elif defined(__APPLE__)
    fprintf(stderr, "\n");
    fprintf(stderr, "On macOS, you may need to grant camera permissions:\n");
    fprintf(stderr,
            "* Say \"yes\" to the popup about system camera access that you see when running this program for the "
            "first time.\n");
    fprintf(stderr,
            "* If you said \"no\" to the popup, go to System Preferences > Security & Privacy > Privacy > Camera.\n");
    fprintf(stderr,
            "   Now flip the switch next to your terminal application in that privacy list to allow ascii-chat to "
            "access your camera.\n");
    fprintf(stderr, "   Then just run this program again.\n");
    fflush(stderr);
    exit(ASCIICHAT_ERR_WEBCAM);
#elif defined(_WIN32)
    if (result == ASCIICHAT_ERR_WEBCAM_IN_USE) {
      // Device is in use by another application - this is a fatal error on Windows
      fprintf(stderr, "\n");
      fprintf(stderr, "Webcam is already in use by another application.\n");
      fprintf(stderr, "Windows allows only one application to access the webcam at a time.\n");
      fprintf(stderr, "\n");
      fprintf(stderr, "To use ASCII-Chat with multiple clients, try these alternatives:\n");
      fprintf(stderr, "  --test-pattern    Generate a colorful test pattern instead of using webcam\n");
      fprintf(stderr, "  --file VIDEO.mp4  Use a video file as input (to be implemented)\n");
      fprintf(stderr, "\n");
      fprintf(stderr, "Example: ascii-chat-client --test-pattern\n");
      fflush(stderr);
      exit(ASCIICHAT_ERR_WEBCAM_IN_USE);
    } else {
      // Other webcam errors - general failure
      fprintf(stderr, "\n");
      fprintf(stderr, "On Windows, this might be because:\n");
      fprintf(stderr, "* Camera permissions are not granted\n");
      fprintf(stderr, "* Camera driver issues\n");
      fprintf(stderr, "* No webcam device found\n");
      fflush(stderr);
      exit(ASCIICHAT_ERR_WEBCAM);
    }
#endif

    exit(ASCIICHAT_ERR_WEBCAM);
  }

  // Get and store image dimensions for aspect ratio calculations
  int width, height;
  if (webcam_get_dimensions(global_webcam_ctx, &width, &height) == 0) {
    last_image_width = (unsigned short int)width;
    last_image_height = (unsigned short int)height;
    log_info("Webcam opened successfully! Resolution: %dx%d", width, height);
  } else {
    log_error("Webcam opened but failed to get dimensions");
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
      log_error("Failed to allocate test pattern frame");
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
        pixel->r = (pixel->r + diagonal) / 2;
        pixel->g = (pixel->g + diagonal) / 2;
        pixel->b = (pixel->b + diagonal) / 2;

        // Add grid lines for visual separation
        if (x % 160 == 0 || y % 120 == 0) {
          pixel->r = 0;
          pixel->g = 0;
          pixel->b = 0;
        }
      }
    }

    // Add a center cross to help with alignment
    int center_x = test_frame->w / 2;
    int center_y = test_frame->h / 2;
    for (int i = 0; i < test_frame->w; i++) {
      rgb_t *pixel = &test_frame->pixels[center_y * test_frame->w + i];
      pixel->r = 255;
      pixel->g = 255;
      pixel->b = 255;
    }
    for (int i = 0; i < test_frame->h; i++) {
      rgb_t *pixel = &test_frame->pixels[i * test_frame->w + center_x];
      pixel->r = 255;
      pixel->g = 255;
      pixel->b = 255;
    }

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

    // Update dimensions for aspect ratio calculations
    last_image_width = (unsigned short int)test_frame->w;
    last_image_height = (unsigned short int)test_frame->h;

    return test_frame;
  }

  if (!global_webcam_ctx) {
    log_error("[WEBCAM_READ] ERROR: Webcam not initialized - global_webcam_ctx is NULL");
    return NULL;
  }

  image_t *frame = webcam_read_context(global_webcam_ctx);

  if (!frame) {
    // Enable debug to see what's happening
    static int null_count = 0;
    null_count++;
    if (null_count % 100 == 0) {
      log_info("DEBUG: webcam_read_context returned NULL (count=%d)", null_count);
    }
    return NULL;
  }

  // Apply horizontal flip if requested
  if (opt_webcam_flip) {
    // Flip the image horizontally
    for (int y = 0; y < frame->h; y++) {
      for (int x = 0; x < frame->w / 2; x++) {
        rgb_t temp = frame->pixels[y * frame->w + x];                                       // Store left pixel
        frame->pixels[y * frame->w + x] = frame->pixels[y * frame->w + (frame->w - 1 - x)]; // Move right pixel to left
        frame->pixels[y * frame->w + (frame->w - 1 - x)] = temp; // Move stored left pixel to right
      }
    }
  }

  // Update dimensions for aspect ratio calculations
  last_image_width = (unsigned short int)frame->w;
  last_image_height = (unsigned short int)frame->h;

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

// Fallback implementations for unsupported platforms
#if !defined(__linux__) && !defined(__APPLE__) && !defined(_WIN32)
int webcam_init_context(webcam_context_t **ctx, unsigned short int device_index) {
  (void)ctx;
  (void)device_index;
  log_error("Webcam platform not supported on this system");
  exit(ASCIICHAT_ERR_WEBCAM);
  return -1;
}

void webcam_cleanup_context(webcam_context_t *ctx) {
  (void)ctx;
  exit(ASCIICHAT_ERR_WEBCAM);
  log_warn("Webcam cleanup called on unsupported platform");
}

image_t *webcam_read_context(webcam_context_t *ctx) {
  (void)ctx;
  log_error("Webcam read not supported on this platform");
  exit(ASCIICHAT_ERR_WEBCAM);
  return NULL;
}

int webcam_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
  (void)ctx;
  (void)width;
  (void)height;
  log_error("Webcam get dimensions not supported on this platform");
  exit(ASCIICHAT_ERR_WEBCAM);
  return -1;
}
#endif
