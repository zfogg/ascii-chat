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
#include "options/completions/completions.h"
#include "options/schema.h"
#include "platform/terminal.h"
#include "platform/question.h"
#include "platform/stat.h"
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
    log_plain_stderr("Error: Failed to enumerate webcam devices");
    exit(ERROR_WEBCAM);
  }

  if (device_count == 0) {
    log_plain_stderr("%s", colored_string(LOG_COLOR_ERROR, "No webcam devices found."));
  } else {
    log_plain_stderr("%s", colored_string(LOG_COLOR_DEV, "Available Webcam Devices:"));
    for (unsigned int i = 0; i < device_count; i++) {
      char index_str[32];
      safe_snprintf(index_str, sizeof(index_str), "%u", devices[i].index);
      log_plain_stderr("  %s %s", colored_string(LOG_COLOR_GREY, index_str), devices[i].name);
    }
  }

  webcam_free_device_list(devices);
  exit(0);
}

// ============================================================================
// Audio Device Actions
// ============================================================================

void action_list_microphones(void) {
  audio_device_info_t *devices = NULL;
  unsigned int device_count = 0;

  asciichat_error_t result = audio_list_input_devices(&devices, &device_count);
  if (result != ASCIICHAT_OK) {
    log_plain_stderr("Error: Failed to enumerate audio input devices");
    exit(ERROR_AUDIO);
  }

  if (device_count == 0) {
    log_plain_stderr("%s", colored_string(LOG_COLOR_ERROR, "No microphone devices found."));
  } else {
    log_plain_stderr("%s", colored_string(LOG_COLOR_DEV, "Available Microphone Devices:"));
    for (unsigned int i = 0; i < device_count; i++) {
      char index_str[32];
      safe_snprintf(index_str, sizeof(index_str), "%d", devices[i].index);
      char device_line[512];
      char *line_ptr = device_line;
      int remaining = sizeof(device_line);
      safe_snprintf(line_ptr, remaining, "  %s %s", colored_string(LOG_COLOR_GREY, index_str), devices[i].name);
      if (devices[i].is_default_input) {
        size_t len = strlen(device_line);
        line_ptr = device_line + len;
        remaining = sizeof(device_line) - (int)len;
        safe_snprintf(line_ptr, remaining, " %s", colored_string(LOG_COLOR_INFO, "(default)"));
      }
      log_plain_stderr("%s", device_line);
    }
  }

  audio_free_device_list(devices);
  exit(0);
}

void action_list_speakers(void) {
  audio_device_info_t *devices = NULL;
  unsigned int device_count = 0;

  asciichat_error_t result = audio_list_output_devices(&devices, &device_count);
  if (result != ASCIICHAT_OK) {
    log_plain_stderr("Error: Failed to enumerate audio output devices");
    exit(ERROR_AUDIO);
  }

  if (device_count == 0) {
    log_plain_stderr("%s", colored_string(LOG_COLOR_ERROR, "No speaker devices found."));
  } else {
    log_plain_stderr("%s", colored_string(LOG_COLOR_DEV, "Available Speaker Devices:"));
    for (unsigned int i = 0; i < device_count; i++) {
      char index_str[32];
      safe_snprintf(index_str, sizeof(index_str), "%d", devices[i].index);
      char device_line[512];
      char *line_ptr = device_line;
      int remaining = sizeof(device_line);
      safe_snprintf(line_ptr, remaining, "  %s %s", colored_string(LOG_COLOR_GREY, index_str), devices[i].name);
      if (devices[i].is_default_output) {
        size_t len = strlen(device_line);
        line_ptr = device_line + len;
        remaining = sizeof(device_line) - (int)len;
        safe_snprintf(line_ptr, remaining, " %s", colored_string(LOG_COLOR_INFO, "(default)"));
      }
      log_plain_stderr("%s", device_line);
    }
  }

  audio_free_device_list(devices);
  exit(0);
}

// ============================================================================
// Terminal Capabilities Action
// ============================================================================

void action_show_capabilities(void) {
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Get terminal width and height via platform detection
  unsigned short width = 110, height = 70; // Defaults
  asciichat_error_t size_result = get_terminal_size(&width, &height);
  (void)size_result; // Ignore error, we'll use defaults

  // Use fprintf directly to ensure output appears, not log_plain which may have issues
  printf("Terminal Capabilities:\n");
  printf("  Terminal Size: %ux%u\n", width, height);
  printf("  Color Level: %s\n", terminal_color_level_name(caps.color_level));
  printf("  Max Colors: %u\n", caps.color_count);
  printf("  UTF-8 Support: %s\n", caps.utf8_support ? "Yes" : "No");

  const char *render_mode_str = "unknown";
  if (caps.render_mode == RENDER_MODE_FOREGROUND) {
    render_mode_str = "foreground";
  } else if (caps.render_mode == RENDER_MODE_BACKGROUND) {
    render_mode_str = "background";
  } else if (caps.render_mode == RENDER_MODE_HALF_BLOCK) {
    render_mode_str = "half-block";
  }
  printf("  Render Mode: %s\n", render_mode_str);
  printf("  TERM: %s\n", caps.term_type);
  printf("  COLORTERM: %s\n", strlen(caps.colorterm) ? caps.colorterm : "(not set)");
  printf("  Detection Reliable: %s\n", caps.detection_reliable ? "Yes" : "No");
  printf("  Capabilities Bitmask: 0x%08x\n", caps.capabilities);
  fflush(stdout);

  exit(0);
}

// ============================================================================
// Version Action
// ============================================================================

void action_show_version(void) {
  log_plain_stderr("ascii-chat %s (%s, %s)", ASCII_CHAT_VERSION_FULL, ASCII_CHAT_BUILD_TYPE, ASCII_CHAT_BUILD_DATE);
  log_plain_stderr("");
  log_plain_stderr("Built with:");

#ifdef __clang__
  log_plain_stderr("  Compiler: Clang %s", __clang_version__);
#elif defined(__GNUC__)
  log_plain_stderr("  Compiler: GCC %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  log_plain_stderr("  Compiler: MSVC %d", _MSC_VER);
#else
  log_plain_stderr("  Compiler: Unknown");
#endif

#ifdef USE_MUSL
  log_plain_stderr("  C Library: musl");
#elif defined(__GLIBC__)
  log_plain_stderr("  C Library: glibc %d.%d", __GLIBC__, __GLIBC_MINOR__);
#elif defined(_WIN32)
  log_plain_stderr("  C Library: MSVCRT");
#elif defined(__APPLE__)
  log_plain_stderr("  C Library: libSystem");
#else
  log_plain_stderr("  C Library: Unknown");
#endif

  log_plain_stderr("");
  log_plain_stderr("For more information: https://github.com/zfogg/ascii-chat");

  exit(0);
}

// ============================================================================
// Help Actions
// ============================================================================

void action_help_server(void) {
  usage(stdout, MODE_SERVER);
  exit(0);
}

void action_help_client(void) {
  usage(stdout, MODE_CLIENT);
  exit(0);
}

void action_help_mirror(void) {
  usage(stdout, MODE_MIRROR);
  exit(0);
}

void action_help_acds(void) {
  usage(stdout, MODE_DISCOVERY_SERVICE);
  exit(0);
}

void action_help_discovery(void) {
  usage(stdout, MODE_DISCOVERY);
  exit(0);
}

// ============================================================================
// Man Page Generation Action
// ============================================================================

void action_create_manpage(const char *output_path) {
  // Get binary-level config
  const options_config_t *config = options_preset_unified(NULL, NULL);
  if (!config) {
    log_plain_stderr("Error: Failed to get binary options config");
    exit(ERROR_FILE_OPERATION);
  }

  // Determine output path: use provided path, or NULL for stdout
  const char *path_to_use = NULL;
  if (output_path && strlen(output_path) > 0 && strcmp(output_path, "-") != 0) {
    // User provided explicit path - use it
    path_to_use = output_path;
  } else {
    // No path or "-" means stdout
    path_to_use = NULL;
  }

  // Generate merged man page from embedded or filesystem resources
  // Resources are loaded automatically based on build type:
  // - Production (Release): From embedded binary data
  // - Development (Debug): From filesystem files
  asciichat_error_t err =
      options_config_generate_manpage_merged(config, "ascii-chat", NULL, path_to_use, "Video chat in your terminal");

  if (err != ASCIICHAT_OK) {
    asciichat_error_context_t err_ctx;
    if (HAS_ERRNO(&err_ctx)) {
      log_plain_stderr("Error: %s", err_ctx.context_message);
    } else {
      log_plain_stderr("Error: Failed to generate man page");
    }
    exit(ERROR_FILE_OPERATION);
  }

  if (path_to_use) {
    log_plain_stderr("Man page written to: %s", path_to_use);
  } else {
    log_plain_stderr("Man page written to stdout");
  }
  exit(0);
}

// ============================================================================
// Config Creation Action
// ============================================================================

void action_create_config(const char *output_path) {
  // Build the schema first so config_create_default can generate options from it
  const options_config_t *unified_config = options_preset_unified(NULL, NULL);
  if (unified_config) {
    asciichat_error_t schema_build_result = config_schema_build_from_configs(&unified_config, 1);
    if (schema_build_result != ASCIICHAT_OK) {
      // Schema build failed, but continue anyway
      (void)schema_build_result;
    }
    options_config_destroy(unified_config);
  }

  options_t opts = {0};
  const char *config_path = NULL;

  // Determine output path: use provided path, or NULL for stdout
  if (output_path && strlen(output_path) > 0 && strcmp(output_path, "-") != 0) {
    // User provided explicit path - use it as-is
    config_path = output_path;
  } else {
    // No path or "-" means stdout
    config_path = NULL;
  }

  // Create config with default options
  asciichat_error_t result = config_create_default(config_path, &opts);
  if (result != ASCIICHAT_OK) {
    asciichat_error_context_t err_ctx;
    if (HAS_ERRNO(&err_ctx)) {
      log_plain_stderr("Error creating config: %s", err_ctx.context_message);
    } else {
      log_plain_stderr("Error: Failed to create config file");
    }
    exit(ERROR_CONFIG);
  }

  if (config_path) {
    log_plain_stderr("Created default config file at: %s", config_path);
  } else {
    log_plain_stderr("Config written to stdout");
  }
  exit(0);
}

// ============================================================================
// Shell Completions Action
// ============================================================================

void action_completions(const char *shell_name, const char *output_path) {
  if (!shell_name || strlen(shell_name) == 0) {
    log_plain_stderr("Error: --completions requires shell name (bash, fish, zsh, powershell)");
    exit(ERROR_USAGE);
  }

  completion_format_t format = completions_parse_shell_name(shell_name);
  if (format == COMPLETION_FORMAT_UNKNOWN) {
    log_plain_stderr("Error: Unknown shell '%s' (supported: bash, fish, zsh, powershell)", shell_name);
    exit(ERROR_USAGE);
  }

  FILE *output = stdout;
  bool should_close = false;

  // Determine output: use provided path if given and not "-", otherwise stdout
  if (output_path && strlen(output_path) > 0 && strcmp(output_path, "-") != 0) {
    // Check if file already exists and prompt for confirmation
    struct stat st;
    if (stat(output_path, &st) == 0) {
      // File exists - ask user if they want to overwrite
      log_plain("Completions file already exists: %s", output_path);

      bool overwrite = platform_prompt_yes_no("Overwrite", false); // Default to No
      if (!overwrite) {
        log_plain("Completions generation cancelled.");
        exit(0);
      }

      log_plain("Overwriting existing completions file...");
    }

    output = fopen(output_path, "w");
    if (!output) {
      log_plain_stderr("Error: Failed to open %s for writing", output_path);
      exit(ERROR_FILE_OPERATION);
    }
    should_close = true;
  }

  asciichat_error_t result = completions_generate_for_shell(format, output);

  if (should_close) {
    fclose(output);
  }

  if (result != ASCIICHAT_OK) {
    log_plain_stderr("Error: Failed to generate %s completions", completions_get_shell_name(format));
    exit(ERROR_USAGE);
  }

  // Silently exit - completions are written (either to file or stdout)
  exit(0);
}
