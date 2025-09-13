#include <stdio.h>
#include <stdlib.h>

#include "webcam.h"
#include "common.h"
#include "options.h"

static webcam_context_t *global_webcam_ctx = NULL;

int webcam_init(unsigned short int webcam_index) {
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

  int result = -1;
  if ((result = webcam_platform_init(&global_webcam_ctx, webcam_index)) != 0) {
    log_error("Failed to connect to webcam");

    // Platform-specific error messages
#ifdef __linux__
    log_error("On Linux, make sure:");
    log_error("* Your user is in the 'video' group: sudo usermod -a -G video $USER");
    log_error("* The camera device exists: ls /dev/video*");
    log_error("* No other application is using the camera");
#elif defined(__APPLE__)
    log_error("On macOS, you may need to grant camera permissions:");
    log_error("* Say \"yes\" to the popup about system camera access that you see when running this program for the "
              "first time.");
    log_error("* If you said \"no\" to the popup, go to System Preferences > Security & Privacy > Privacy > Camera.");
    log_error("   Now flip the switch next to your terminal application in that privacy list to allow ascii-chat to "
              "access your camera.");
    log_error("   Then just run this program again.");
#endif

    exit(ASCIICHAT_ERR_WEBCAM);
  }

  // Get and store image dimensions for aspect ratio calculations
  int width, height;
  if (webcam_platform_get_dimensions(global_webcam_ctx, &width, &height) == 0) {
    last_image_width = (unsigned short int)width;
    last_image_height = (unsigned short int)height;
    log_info("Webcam opened successfully! Resolution: %dx%d", width, height);
  } else {
    log_error("Webcam opened but failed to get dimensions");
  }

  return result;
}

image_t *webcam_read(void) {
  static int read_count = 0;
  read_count++;

  if (!global_webcam_ctx) {
    log_error("[WEBCAM_READ] ERROR: Webcam not initialized - global_webcam_ctx is NULL");
    return NULL;
  }

  image_t *frame = webcam_platform_read(global_webcam_ctx);

  if (!frame) {
    // Enable debug to see what's happening
    static int null_count = 0;
    null_count++;
    if (null_count % 100 == 0) {
      log_info("DEBUG: webcam_platform_read returned NULL (count=%d)", null_count);
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
  if (global_webcam_ctx) {
    webcam_platform_cleanup(global_webcam_ctx);
    global_webcam_ctx = NULL;
    // log_info("Webcam resources released");
  } else {
    log_info("Webcam was not opened, nothing to release");
  }
}

// Fallback implementations for unsupported platforms
#if !defined(__linux__) && !defined(__APPLE__) && !defined(_WIN32)
int webcam_platform_init(webcam_context_t **ctx, unsigned short int device_index) {
  (void)ctx;
  (void)device_index;
  log_error("Webcam platform not supported on this system");
  return -1;
}

void webcam_platform_cleanup(webcam_context_t *ctx) {
  (void)ctx;
  log_warn("Webcam cleanup called on unsupported platform");
}

image_t *webcam_platform_read(webcam_context_t *ctx) {
  (void)ctx;
  log_error("Webcam read not supported on this platform");
  return NULL;
}

int webcam_platform_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
  (void)ctx;
  (void)width;
  (void)height;
  log_error("Webcam get dimensions not supported on this platform");
  return -1;
}
#endif
