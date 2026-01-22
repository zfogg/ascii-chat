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
#include "options/common.h"
#include "options/manpage.h"
#include "options/presets.h"
#include "options/config.h"
#include "platform/terminal.h"
#include "version.h"
#include "video/webcam/webcam.h"
#include "audio/audio.h"
#include "util/string.h"
#include "util/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_DEV, "Available Webcam Devices:"));
    for (unsigned int i = 0; i < device_count; i++) {
      char index_str[32];
      (void)snprintf(index_str, sizeof(index_str), "%u", devices[i].index);
      (void)fprintf(stdout, "  %s %s\n", colored_string(LOG_COLOR_GREY, index_str), devices[i].name);
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
    (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_DEV, "Available Microphone Devices:"));
    for (unsigned int i = 0; i < device_count; i++) {
      char index_str[32];
      (void)snprintf(index_str, sizeof(index_str), "%d", devices[i].index);
      (void)fprintf(stdout, "  %s %s", colored_string(LOG_COLOR_GREY, index_str), devices[i].name);
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
    (void)fprintf(stdout, "%s\n", colored_string(LOG_COLOR_DEV, "Available Speaker Devices:"));
    for (unsigned int i = 0; i < device_count; i++) {
      char index_str[32];
      (void)snprintf(index_str, sizeof(index_str), "%d", devices[i].index);
      (void)fprintf(stdout, "  %s %s", colored_string(LOG_COLOR_GREY, index_str), devices[i].name);
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

#define PRINT_CAP_LINE(label, value_str, value_color)                                                                  \
  do {                                                                                                                 \
    (void)fprintf(stdout, "  %s", colored_string(LOG_COLOR_GREY, label));                                              \
    for (size_t i = strlen(label); i < max_label_width; i++) {                                                         \
      (void)fprintf(stdout, " ");                                                                                      \
    }                                                                                                                  \
    (void)fprintf(stdout, " %s\n", colored_string(value_color, value_str));                                            \
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
  PRINT_CAP_LINE(label_detection, caps.detection_reliable ? "Yes" : "No",
                 caps.detection_reliable ? LOG_COLOR_INFO : LOG_COLOR_ERROR);

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
  usage(stdout, MODE_SERVER);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_client(void) {
  usage(stdout, MODE_CLIENT);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_mirror(void) {
  usage(stdout, MODE_MIRROR);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_acds(void) {
  usage(stdout, MODE_DISCOVERY_SERVER);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_discovery(void) {
  usage(stdout, MODE_DISCOVERY);
  (void)fflush(stdout);
  _exit(0);
}

// ============================================================================
// Man Page Generation Action
// ============================================================================

void action_create_manpage(void) {
  const char *template_path = "share/man/man1/ascii-chat.1.in";
  const char *output_path = template_path;

  // Get binary-level config
  const options_config_t *config = options_preset_unified(NULL, NULL);
  if (!config) {
    (void)fprintf(stderr, "Error: Failed to get binary options config\n");
    _exit(1);
  }

  // Generate merged man page (use default content file)
  const char *content_file = "share/man/man1/ascii-chat.1.content";
  asciichat_error_t err = options_config_generate_manpage_merged(
      config, "ascii-chat", NULL, output_path, "Video chat in your terminal", template_path, content_file);

  if (err != ASCIICHAT_OK) {
    asciichat_error_context_t err_ctx;
    if (HAS_ERRNO(&err_ctx)) {
      (void)fprintf(stderr, "Error: %s\n", err_ctx.context_message);
    } else {
      (void)fprintf(stderr, "Error: Failed to generate man page template\n");
    }
    _exit(1);
  }

  (void)fprintf(stdout, "Generated merged man page template: %s\n", output_path);
  (void)fprintf(stdout, "Review AUTO sections - manual edits will be lost on regeneration.\n");
  (void)fflush(stdout);
  _exit(0);
}

// ============================================================================
// Config Creation Action
// ============================================================================

void action_create_config(void) {
  // Get binary-level config to access options
  const options_config_t *config = options_preset_unified(NULL, NULL);
  if (!config) {
    (void)fprintf(stderr, "Error: Failed to get binary options config\n");
    _exit(1);
  }

  // Parse just to get the config path if provided
  // For now, use default path (can be extended to read from argv if needed)
  options_t opts = {0};
  char config_path[PLATFORM_MAX_PATH_LENGTH] = {0};

  // Try to get config path from options (if --config was set)
  // For --config-create, we'll use default path unless extended
  char *config_dir = get_config_dir();
  if (!config_dir) {
    (void)fprintf(stderr, "Error: Failed to determine default config directory\n");
    _exit(1);
  }
  snprintf(config_path, sizeof(config_path), "%sconfig.toml", config_dir);
  SAFE_FREE(config_dir);

  // Create config with default options
  asciichat_error_t result = config_create_default(config_path, &opts);
  if (result != ASCIICHAT_OK) {
    asciichat_error_context_t err_ctx;
    if (HAS_ERRNO(&err_ctx)) {
      (void)fprintf(stderr, "Error creating config: %s\n", err_ctx.context_message);
    } else {
      (void)fprintf(stderr, "Error: Failed to create config file at %s\n", config_path);
    }
    _exit(1);
  }

  (void)fprintf(stdout, "Created default config file at: %s\n", config_path);
  (void)fflush(stdout);
  _exit(0);
}
