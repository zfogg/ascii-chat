/**
 * @file actions.c
 * @brief Action option callbacks for ascii-chat
 * @ingroup options
 *
 * Action options are deferred until after all options are fully parsed and initialized.
 * This ensures that options like --width and --height are properly reflected in action
 * output (e.g., --show-capabilities displays the final terminal dimensions).
 *
 * Examples: --list-webcams, --list-microphones, --list-speakers, --show-capabilities
 */

#include <ascii-chat/options/actions.h>
#include <string.h>
#include <stddef.h>

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/network/update_checker.h>
#include <ascii-chat/options/manpage.h>
#include <ascii-chat/options/presets.h>
#include <ascii-chat/options/config.h>
#include <ascii-chat/options/completions/completions.h>
#include <ascii-chat/options/schema.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/platform/stat.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/version.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/path.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Deferred Action Tracking System
// ============================================================================

/**
 * @brief Global state for deferred actions
 *
 * Tracks which action to execute and its arguments after options initialization.
 * Only the first action found is stored - subsequent actions are ignored.
 */
static struct {
  deferred_action_t action;
  action_args_t args;
  bool has_action;
} g_deferred_action_state = {
    .action = ACTION_NONE,
    .args = {0},
    .has_action = false,
};

void actions_defer(deferred_action_t action, const action_args_t *args) {
  // Only remember the first action found
  if (g_deferred_action_state.has_action) {
    return; // Action already deferred, ignore subsequent actions
  }

  g_deferred_action_state.action = action;
  g_deferred_action_state.has_action = true;

  // Copy arguments if provided
  if (args) {
    memcpy(&g_deferred_action_state.args, args, sizeof(action_args_t));
  } else {
    memset(&g_deferred_action_state.args, 0, sizeof(action_args_t));
  }
}

deferred_action_t actions_get_deferred(void) {
  return g_deferred_action_state.action;
}

const action_args_t *actions_get_args(void) {
  if (!g_deferred_action_state.has_action) {
    return NULL;
  }
  return &g_deferred_action_state.args;
}

// ============================================================================
// Webcam Action
// ============================================================================

void action_list_webcams(void) {
  // Defer execution until after options are fully parsed
  actions_defer(ACTION_LIST_WEBCAMS, NULL);
}

/**
 * @brief Internal implementation of list webcams action
 *
 * Called during STAGE 8 of options_init() after all initialization is complete.
 * Enumerates and displays available webcam devices, then exits.
 */
static void execute_list_webcams(void) {
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
  // Defer execution until after options are fully parsed
  actions_defer(ACTION_LIST_MICROPHONES, NULL);
}

/**
 * @brief Internal implementation of list microphones action
 *
 * Called during STAGE 8 of options_init() after all initialization is complete.
 * Enumerates and displays available microphone devices, then exits.
 */
static void execute_list_microphones(void) {
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
  // Defer execution until after options are fully parsed
  actions_defer(ACTION_LIST_SPEAKERS, NULL);
}

/**
 * @brief Internal implementation of list speakers action
 *
 * Called during STAGE 8 of options_init() after all initialization is complete.
 * Enumerates and displays available speaker devices, then exits.
 */
static void execute_list_speakers(void) {
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

/**
 * @brief Execute show capabilities immediately (for early binary-level execution)
 */
void action_show_capabilities_immediate(void) {
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Determine if we should use colored output
  bool use_colors = terminal_should_color_output(STDOUT_FILENO);

  // Override color level if colors should not be shown
  if (!use_colors) {
    caps.color_level = TERM_COLOR_NONE;
    caps.color_count = 0;
    caps.capabilities &= ~((uint32_t)(TERM_CAP_COLOR_TRUE | TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16));
  }

  // Use detected terminal size (immediate execution doesn't have parsed options yet)
  terminal_size_t size;
  terminal_get_size(&size);
  unsigned short width = size.cols;
  unsigned short height = size.rows;

  log_color_t label_color = use_colors ? LOG_COLOR_GREY : LOG_COLOR_GREY;
  log_color_t string_color = use_colors ? LOG_COLOR_DEBUG : LOG_COLOR_GREY;
  log_color_t good_color = use_colors ? LOG_COLOR_INFO : LOG_COLOR_GREY;
  log_color_t bad_color = use_colors ? LOG_COLOR_ERROR : LOG_COLOR_GREY;
  log_color_t number_color = use_colors ? LOG_COLOR_FATAL : LOG_COLOR_GREY;

  printf("%s\n", colored_string(LOG_COLOR_WARN, "Terminal Capabilities:"));

  char size_buf[64];
  snprintf(size_buf, sizeof(size_buf), "%ux%u", width, height);
  printf("  %s: %s\n", colored_string(label_color, "Terminal Size"), colored_string(number_color, size_buf));

  const char *color_level_name = terminal_color_level_name(caps.color_level);
  log_color_t color_level_color = (caps.color_level == TERM_COLOR_NONE) ? bad_color : string_color;
  printf("  %s: %s\n", colored_string(label_color, "Color Level"),
         colored_string(color_level_color, (char *)color_level_name));

  char colors_buf[64];
  snprintf(colors_buf, sizeof(colors_buf), "%u", caps.color_count);
  printf("  %s: %s\n", colored_string(label_color, "Max Colors"), colored_string(number_color, colors_buf));

  log_color_t utf8_color = caps.utf8_support ? good_color : bad_color;
  printf("  %s: %s\n", colored_string(label_color, "UTF-8 Support"),
         colored_string(utf8_color, (char *)(caps.utf8_support ? "Yes" : "No")));

  const char *render_mode_str = "unknown";
  if (caps.render_mode == RENDER_MODE_FOREGROUND) {
    render_mode_str = "foreground";
  } else if (caps.render_mode == RENDER_MODE_BACKGROUND) {
    render_mode_str = "background";
  } else if (caps.render_mode == RENDER_MODE_HALF_BLOCK) {
    render_mode_str = "half-block";
  }
  printf("  %s: %s\n", colored_string(label_color, "Render Mode"),
         colored_string(string_color, (char *)render_mode_str));
  printf("  %s: %s\n", colored_string(label_color, "TERM"), colored_string(string_color, (char *)caps.term_type));
  printf("  %s: %s\n", colored_string(label_color, "COLORTERM"),
         colored_string(string_color, (char *)(strlen(caps.colorterm) ? caps.colorterm : "(not set)")));

  log_color_t reliable_color = caps.detection_reliable ? good_color : bad_color;
  printf("  %s: %s\n", colored_string(label_color, "Detection Reliable"),
         colored_string(reliable_color, (char *)(caps.detection_reliable ? "Yes" : "No")));

  char bitmask_buf[64];
  snprintf(bitmask_buf, sizeof(bitmask_buf), "0x%08x", caps.capabilities);
  printf("  %s: %s\n", colored_string(label_color, "Capabilities Bitmask"), colored_string(number_color, bitmask_buf));

  fflush(stdout);
  exit(0);
}

void action_show_capabilities(void) {
  // Defer execution until after options are fully parsed and dimensions updated
  // This ensures --width and --height flags are properly reflected in the output
  actions_defer(ACTION_SHOW_CAPABILITIES, NULL);
}

// ============================================================================
// Update Check Action
// ============================================================================

/**
 * @brief Execute update check immediately (for early binary-level execution)
 */
void action_check_update_immediate(void) {
  printf("Checking for updates...\n");
  update_check_result_t result;
  asciichat_error_t err = update_check_perform(&result);
  if (err != ASCIICHAT_OK) {
    printf("\nFailed to check for updates.\n\n");
    exit(1);
  }
  if (result.update_available) {
    char notification[1024];
    update_check_format_notification(&result, notification, sizeof(notification));
    printf("\n%s\n\n", notification);
  } else {
    printf("\nYou are already on the latest version: %s (%.8s)\n\n", result.current_version, result.current_sha);
  }
  exit(0);
}

void action_check_update(void) {
  // Defer execution until after options are fully parsed
  actions_defer(ACTION_CHECK_UPDATE, NULL);
}

/**
 * @brief Internal implementation of show capabilities action
 *
 * Called during STAGE 8 of options_init() after all options are parsed,
 * dimensions are calculated, and options are published via RCU.
 *
 * Displays terminal capabilities including color support, UTF-8 support,
 * detected terminal type, and the final dimensions (which respect --width
 * and --height flags), then exits.
 */
static void execute_show_capabilities(void) {
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Determine if we should use colored output
  bool use_colors = terminal_should_color_output(STDOUT_FILENO);

  // Override color level if colors should not be shown
  if (!use_colors) {
    // User requested no colors or output is piped or CLAUDECODE is set
    caps.color_level = TERM_COLOR_NONE;
    caps.color_count = 0;
    caps.capabilities &= ~((uint32_t)(TERM_CAP_COLOR_TRUE | TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16));
  }

  // Get width and height from parsed options (respects --width and --height flags)
  const options_t *opts = options_get();
  unsigned short width = opts ? opts->width : 110;
  unsigned short height = opts ? opts->height : 70;

  // Color scheme:
  // - Labels: grey
  // - Regular string values: blue (DEBUG - cyan/blue)
  // - Affirmative values (Yes, true): green (INFO)
  // - Bad/negative values (No, false, error): red (ERROR)
  // - Numbers and hex: magenta (FATAL)
  log_color_t label_color = use_colors ? LOG_COLOR_GREY : LOG_COLOR_GREY;
  log_color_t string_color = use_colors ? LOG_COLOR_DEBUG : LOG_COLOR_GREY;
  log_color_t good_color = use_colors ? LOG_COLOR_INFO : LOG_COLOR_GREY;
  log_color_t bad_color = use_colors ? LOG_COLOR_ERROR : LOG_COLOR_GREY;
  log_color_t number_color = use_colors ? LOG_COLOR_FATAL : LOG_COLOR_GREY;

  printf("%s\n", colored_string(LOG_COLOR_WARN, "Terminal Capabilities:"));

  char size_buf[64];
  snprintf(size_buf, sizeof(size_buf), "%ux%u", width, height);
  printf("  %s: %s\n", colored_string(label_color, "Terminal Size"), colored_string(number_color, size_buf));

  // Color Level: green if good, red if none/bad
  const char *color_level_name = terminal_color_level_name(caps.color_level);
  log_color_t color_level_color = (caps.color_level == TERM_COLOR_NONE) ? bad_color : string_color;
  printf("  %s: %s\n", colored_string(label_color, "Color Level"),
         colored_string(color_level_color, (char *)color_level_name));

  char colors_buf[64];
  snprintf(colors_buf, sizeof(colors_buf), "%u", caps.color_count);
  printf("  %s: %s\n", colored_string(label_color, "Max Colors"), colored_string(number_color, colors_buf));

  // UTF-8: green if Yes, red if No
  log_color_t utf8_color = caps.utf8_support ? good_color : bad_color;
  printf("  %s: %s\n", colored_string(label_color, "UTF-8 Support"),
         colored_string(utf8_color, (char *)(caps.utf8_support ? "Yes" : "No")));

  const char *render_mode_str = "unknown";
  if (caps.render_mode == RENDER_MODE_FOREGROUND) {
    render_mode_str = "foreground";
  } else if (caps.render_mode == RENDER_MODE_BACKGROUND) {
    render_mode_str = "background";
  } else if (caps.render_mode == RENDER_MODE_HALF_BLOCK) {
    render_mode_str = "half-block";
  }
  printf("  %s: %s\n", colored_string(label_color, "Render Mode"),
         colored_string(string_color, (char *)render_mode_str));
  printf("  %s: %s\n", colored_string(label_color, "TERM"), colored_string(string_color, (char *)caps.term_type));
  printf("  %s: %s\n", colored_string(label_color, "COLORTERM"),
         colored_string(string_color, (char *)(strlen(caps.colorterm) ? caps.colorterm : "(not set)")));

  // Detection Reliable: green if Yes, red if No
  log_color_t reliable_color = caps.detection_reliable ? good_color : bad_color;
  printf("  %s: %s\n", colored_string(label_color, "Detection Reliable"),
         colored_string(reliable_color, (char *)(caps.detection_reliable ? "Yes" : "No")));

  char bitmask_buf[64];
  snprintf(bitmask_buf, sizeof(bitmask_buf), "0x%08x", caps.capabilities);
  printf("  %s: %s\n", colored_string(label_color, "Capabilities Bitmask"), colored_string(number_color, bitmask_buf));

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
    // options_config_destroy(unified_config);
  }

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
  asciichat_error_t result = config_create_default(config_path);
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
  // Suppress memory report for clean output
#if defined(DEBUG_MEMORY) && !defined(NDEBUG)
  debug_memory_set_quiet_mode(true);
#endif

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

    output = platform_fopen(output_path, "w");
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

/**
 * @brief Internal implementation of check update action
 *
 * Called during STAGE 8 of options_init() after all initialization is complete.
 * Checks for updates from GitHub releases and displays results, then exits.
 */
static void execute_check_update(void) {
  printf("Checking for updates...\n");

  update_check_result_t result;
  asciichat_error_t err = update_check_perform(&result);

  if (err != ASCIICHAT_OK) {
    asciichat_error_context_t ctx;
    if (HAS_ERRNO(&ctx)) {
      fprintf(stderr, "Update check failed: %s\n", ctx.context_message);
    }
    exit(1);
  }

  // Display results
  if (result.update_available) {
    char notification[1024];
    update_check_format_notification(&result, notification, sizeof(notification));
    printf("\n%s\n\n", notification);
  } else {
    printf("\nYou are already on the latest version: %s (%.8s)\n\n", result.current_version, result.current_sha);
  }

  exit(0);
}

// ============================================================================
// Deferred Action Execution
// ============================================================================

void actions_execute_deferred(void) {
  deferred_action_t action = actions_get_deferred();

  switch (action) {
  case ACTION_NONE:
    // No action to execute
    break;

  case ACTION_LIST_WEBCAMS:
    execute_list_webcams();
    break;

  case ACTION_LIST_MICROPHONES:
    execute_list_microphones();
    break;

  case ACTION_LIST_SPEAKERS:
    execute_list_speakers();
    break;

  case ACTION_SHOW_CAPABILITIES:
    execute_show_capabilities();
    break;

  case ACTION_CHECK_UPDATE:
    execute_check_update();
    break;

  default:
    log_warn("Unknown deferred action: %d", action);
    break;
  }
}
