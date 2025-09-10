#include <stdio.h>
#include <stdlib.h>

#include "webcam.h"
#include "webcam_platform.h"
#include "common.h"
#include "options.h"

static webcam_context_t *global_webcam_ctx = NULL;

int webcam_init(unsigned short int webcam_index) {
  webcam_platform_type_t platform = webcam_get_platform();

  log_info("Initializing webcam with %s", webcam_platform_name(platform));
  log_info("Attempting to open webcam with index %d using %s...", webcam_index, webcam_platform_name(platform));

  int result = -1;
  if ((result = webcam_platform_init(&global_webcam_ctx, webcam_index)) != 0) {
    log_error("Failed to connect to webcam");

    // Platform-specific error messages
    if (platform == WEBCAM_PLATFORM_V4L2) {
      log_error("On Linux, make sure:");
      log_error("* Your user is in the 'video' group: sudo usermod -a -G video $USER");
      log_error("* The camera device exists: ls /dev/video*");
      log_error("* No other application is using the camera");
    } else if (platform == WEBCAM_PLATFORM_AVFOUNDATION) {
      log_error("On macOS, you may need to grant camera permissions:");
      log_error("* Say \"yes\" to the popup about system camera access that you see when running this program for the "
                "first time.");
      log_error("* If you said \"no\" to the popup, go to System Preferences > Security & Privacy > Privacy > Camera.");
      log_error("   Now flip the switch next to your terminal application in that privacy list to allow ascii-chat to "
                "access your camera.");
      log_error("   Then just run this program again.");
    }

    exit(ASCIICHAT_ERR_WEBCAM);
  }

  // Get and store image dimensions for aspect ratio calculations
  int width, height;
  if (webcam_platform_get_dimensions(global_webcam_ctx, &width, &height) == 0) {
    last_image_width = (unsigned short int)width;
    last_image_height = (unsigned short int)height;
    log_info("Webcam opened successfully! Resolution: %dx%d", width, height);
    log_info("Webcam opened successfully! Resolution: %dx%d", width, height);
  } else {
    log_error("Webcam opened but failed to get dimensions");
  }

  return result;
}

image_t *webcam_read(void) {
  if (!global_webcam_ctx) {
    log_error("ERROR: Webcam not initialized - global_webcam_ctx is NULL");
    return NULL;
  }

  image_t *frame = webcam_platform_read(global_webcam_ctx);
  if (!frame) {
    // Enable debug to see what's happening
    static int null_count = 0;
    null_count++;
    if (null_count % 100 == 0) {
      log_debug("DEBUG: webcam_platform_read returned NULL (count=%d)", null_count);
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
