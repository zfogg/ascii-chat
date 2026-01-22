/**
 * @file actions.c
 * @brief Action option callbacks for ascii-chat
 * @ingroup options
 *
 * Action options execute immediately during parsing and may exit the program.
 * Examples: --list-webcams, --version, --show-capabilities
 */

#include "actions.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/server.h"
#include "options/client.h"
#include "options/mirror.h"
#include "options/discovery.h"
#include "platform/terminal.h"
#include "version.h"
#include "video/webcam/webcam.h"
#include "audio/audio.h"
#include "util/string.h"

#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// Webcam Action
// ============================================================================

void action_list_webcams(void) {
  webcam_device_info_t *devices = NULL;
  unsigned int device_count = 0;

  asciichat_error_t result = webcam_list_devices(&devices, &device_count);
  if (result != ASCIICHAT_OK) {
    (void)fprintf(stderr, "Error: Failed to enumerate webcam devices\n");
    _exit(1);
  }

  if (device_count == 0) {
    (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_ERROR, "No webcam devices found."));
  } else {
    (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_INFO, "Available Webcam Devices:"));
    for (unsigned int i = 0; i < device_count; i++) {
      char index_str[32];
      (void)snprintf(index_str, sizeof(index_str), "%u", devices[i].index);
      (void)fprintf(stdout, "  %s %s\n", colored_string(LOG_COLOR_GREY, index_str), colored_string(LOG_COLOR_DEV, devices[i].name));
    }
  }

  webcam_free_device_list(devices);
  (void)fflush(stdout);
  _exit(0);
}

// ============================================================================
// Audio Device Actions
// ============================================================================

void action_list_microphones(void) {
  audio_device_info_t *devices = NULL;
  unsigned int device_count = 0;

  asciichat_error_t result = audio_list_input_devices(&devices, &device_count);
  if (result != ASCIICHAT_OK) {
    (void)fprintf(stderr, "Error: Failed to enumerate audio input devices\n");
    _exit(1);
  }

  if (device_count == 0) {
    (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_ERROR, "No microphone devices found."));
  } else {
    (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_INFO, "Available Microphone Devices:"));
    for (unsigned int i = 0; i < device_count; i++) {
      char index_str[32];
      (void)snprintf(index_str, sizeof(index_str), "%d", devices[i].index);
      (void)fprintf(stdout, "  %s %s", colored_string(LOG_COLOR_GREY, index_str), colored_string(LOG_COLOR_DEV, devices[i].name));
      if (devices[i].is_default_input) {
        (void)fprintf(stdout, " %s", colored_string(LOG_COLOR_INFO, "(default)"));
      }
      (void)fprintf(stdout, "\n");
    }
  }

  audio_free_device_list(devices);
  (void)fflush(stdout);
  _exit(0);
}

void action_list_speakers(void) {
  audio_device_info_t *devices = NULL;
  unsigned int device_count = 0;

  asciichat_error_t result = audio_list_output_devices(&devices, &device_count);
  if (result != ASCIICHAT_OK) {
    (void)fprintf(stderr, "Error: Failed to enumerate audio output devices\n");
    _exit(1);
  }

  if (device_count == 0) {
    (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_ERROR, "No speaker devices found."));
  } else {
    (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_INFO, "Available Speaker Devices:"));
    for (unsigned int i = 0; i < device_count; i++) {
      char index_str[32];
      (void)snprintf(index_str, sizeof(index_str), "%d", devices[i].index);
      (void)fprintf(stdout, "  %s %s", colored_string(LOG_COLOR_GREY, index_str), colored_string(LOG_COLOR_DEV, devices[i].name));
      if (devices[i].is_default_output) {
        (void)fprintf(stdout, " %s", colored_string(LOG_COLOR_INFO, "(default)"));
      }
      (void)fprintf(stdout, "\n");
    }
  }

  audio_free_device_list(devices);
  (void)fflush(stdout);
  _exit(0);
}

// ============================================================================
// Terminal Capabilities Action
// ============================================================================

void action_show_capabilities(void) {
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Print title with color
  (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_INFO, "Detected Terminal Capabilities:"));

  // Column alignment: calculate max label width
  const char *label_color_level = "Color Level:";
  const char *label_max_colors = "Max Colors:";
  const char *label_utf8 = "UTF-8 Support:";
  const char *label_render_mode = "Render Mode:";
  const char *label_term = "TERM:";
  const char *label_colorterm = "COLORTERM:";
  const char *label_detection = "Detection Reliable:";
  const char *label_bitmask = "Capabilities Bitmask:";

  size_t max_label_width = 0;
  max_label_width = MAX(max_label_width, strlen(label_color_level));
  max_label_width = MAX(max_label_width, strlen(label_max_colors));
  max_label_width = MAX(max_label_width, strlen(label_utf8));
  max_label_width = MAX(max_label_width, strlen(label_render_mode));
  max_label_width = MAX(max_label_width, strlen(label_term));
  max_label_width = MAX(max_label_width, strlen(label_colorterm));
  max_label_width = MAX(max_label_width, strlen(label_detection));
  max_label_width = MAX(max_label_width, strlen(label_bitmask));

#define PRINT_CAP_LINE(label, value_str, value_color) \
  do { \
    (void)fprintf(stdout, "  %s", colored_string(LOG_COLOR_GREY, label)); \
    for (size_t i = strlen(label); i < max_label_width; i++) { \
      (void)fprintf(stdout, " "); \
    } \
    (void)fprintf(stdout, " %s\n", colored_string(value_color, value_str)); \
  } while (0)

  // Print Color Level
  const char *color_level_name = terminal_color_level_name(caps.color_level);
  PRINT_CAP_LINE(label_color_level, color_level_name, LOG_COLOR_DEV);

  // Print Max Colors
  char max_colors_str[32];
  (void)snprintf(max_colors_str, sizeof(max_colors_str), "%u", caps.color_count);
  PRINT_CAP_LINE(label_max_colors, max_colors_str, LOG_COLOR_DEV);

  // Print UTF-8 Support
  PRINT_CAP_LINE(label_utf8, caps.utf8_support ? "Yes" : "No", caps.utf8_support ? LOG_COLOR_INFO : LOG_COLOR_ERROR);

  // Print Render Mode
  const char *render_mode_str;
  if (caps.render_mode == RENDER_MODE_HALF_BLOCK) {
    render_mode_str = "half-block";
  } else if (caps.render_mode == RENDER_MODE_BACKGROUND) {
    render_mode_str = "background";
  } else {
    render_mode_str = "foreground";
  }
  PRINT_CAP_LINE(label_render_mode, render_mode_str, LOG_COLOR_DEV);

  // Print TERM
  PRINT_CAP_LINE(label_term, caps.term_type[0] ? caps.term_type : "Unknown", LOG_COLOR_DEV);

  // Print COLORTERM
  PRINT_CAP_LINE(label_colorterm, caps.colorterm[0] ? caps.colorterm : "(not set)", LOG_COLOR_DEV);

  // Print Detection Reliable
  PRINT_CAP_LINE(label_detection, caps.detection_reliable ? "Yes" : "No", caps.detection_reliable ? LOG_COLOR_INFO : LOG_COLOR_ERROR);

  // Print Capabilities Bitmask
  char bitmask_str[32];
  (void)snprintf(bitmask_str, sizeof(bitmask_str), "0x%08x", caps.capabilities);
  PRINT_CAP_LINE(label_bitmask, bitmask_str, LOG_COLOR_GREY);

#undef PRINT_CAP_LINE

  (void)fflush(stdout);
  _exit(0);
}

// ============================================================================
// Version Action
// ============================================================================

void action_show_version(void) {
  (void)fprintf(stdout, "ascii-chat %s (%s, %s)\n", ASCII_CHAT_VERSION_FULL, ASCII_CHAT_BUILD_TYPE,
                ASCII_CHAT_BUILD_DATE);
  (void)fprintf(stdout, "\n");
  (void)fprintf(stdout, "Built with:\n");

#ifdef __clang__
  (void)fprintf(stdout, "  Compiler: Clang %s\n", __clang_version__);
#elif defined(__GNUC__)
  (void)fprintf(stdout, "  Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  (void)fprintf(stdout, "  Compiler: MSVC %d\n", _MSC_VER);
#else
  (void)fprintf(stdout, "  Compiler: Unknown\n");
#endif

#ifdef USE_MUSL
  (void)fprintf(stdout, "  C Library: musl\n");
#elif defined(__GLIBC__)
  (void)fprintf(stdout, "  C Library: glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#elif defined(_WIN32)
  (void)fprintf(stdout, "  C Library: MSVCRT\n");
#elif defined(__APPLE__)
  (void)fprintf(stdout, "  C Library: libSystem\n");
#else
  (void)fprintf(stdout, "  C Library: Unknown\n");
#endif

  (void)fprintf(stdout, "\n");
  (void)fprintf(stdout, "For more information: https://github.com/zfogg/ascii-chat\n");

  (void)fflush(stdout);
  _exit(0);
}

// ============================================================================
// Help Actions
// ============================================================================

void action_help_server(void) {
  usage_server(stdout);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_client(void) {
  usage_client(stdout);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_mirror(void) {
  usage_mirror(stdout);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_acds(void) {
  usage_acds(stdout);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_discovery(void) {
  usage_discovery(stdout);
  (void)fflush(stdout);
  _exit(0);
}
