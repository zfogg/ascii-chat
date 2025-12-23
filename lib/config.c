/**
 * @file config.c
 * @ingroup config
 * @brief 📋 TOML configuration file parser with schema validation and CLI override support
 */

#include "config.h"
#include "options.h"
#include "util/path.h"
#include "common.h"
#include "platform/terminal.h"
#include "platform/system.h"
#include "crypto/crypto.h"
#include "logging.h"
#include "version.h"
#include "tooling/defer/defer.h"

#include "tomlc17.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#endif

/**
 * @name Internal Macros
 * @{
 */

/**
 * @brief Print configuration warning to stderr
 *
 * Config warnings are printed directly to stderr because logging may not be
 * initialized yet when configuration is loaded. This ensures users see
 * validation errors immediately.
 */
#define CONFIG_WARN(fmt, ...)                                                                                          \
  do {                                                                                                                 \
    (void)fprintf(stderr, "WARNING: Config file: " fmt "\n", ##__VA_ARGS__);                                           \
    (void)fflush(stderr);                                                                                              \
  } while (0)

/**
 * @brief Print configuration debug message
 *
 * Debug messages use the logging system if it's initialized, otherwise
 * they are silently dropped.
 */
#define CONFIG_DEBUG(fmt, ...)                                                                                         \
  do {                                                                                                                 \
    /* Debug messages are only shown in debug builds after logging is initialized */                                   \
    /* Use log_debug which safely checks initialization itself */                                                      \
    log_debug(fmt, ##__VA_ARGS__);                                                                                     \
  } while (0)

/** @} */

/**
 * @name Configuration State Tracking
 * @{
 *
 * These flags track which options were set from the config file. They are
 * primarily used for debugging and logging purposes, as CLI arguments will
 * override config values regardless of these flags.
 */

/** @brief Track if network address was set from config */
static bool config_address_set = false;
/** @brief Track if IPv6 bind address was set from config (server only) */
static bool config_address6_set = false;
/** @brief Track if network port was set from config */
static bool config_port_set = false;
/** @brief Track if client width was set from config */
static bool config_width_set = false;
/** @brief Track if client height was set from config */
static bool config_height_set = false;
/** @brief Track if webcam index was set from config */
static bool config_webcam_index_set = false;
/** @brief Track if webcam flip was set from config */
static bool config_webcam_flip_set = false;
/** @brief Track if color mode was set from config */
static bool config_color_mode_set = false;
/** @brief Track if render mode was set from config */
static bool config_render_mode_set = false;
/** @brief Track if palette type was set from config */
static bool config_palette_set = false;
/** @brief Track if palette chars were set from config */
static bool config_palette_chars_set = false;
/** @brief Track if audio enabled was set from config */
static bool config_audio_enabled_set = false;
/** @brief Track if microphone index was set from config */
static bool config_microphone_index_set = false;
/** @brief Track if speakers index was set from config */
static bool config_speakers_index_set = false;
/** @brief Track if stretch was set from config */
static bool config_stretch_set = false;
/** @brief Track if quiet mode was set from config */
static bool config_quiet_set = false;
/** @brief Track if snapshot mode was set from config */
static bool config_snapshot_mode_set = false;
/** @brief Track if mirror mode was set from config */
static bool config_mirror_mode_set = false;
/** @brief Track if snapshot delay was set from config */
static bool config_snapshot_delay_set = false;
/** @brief Track if log file was set from config */
static bool config_log_file_set = false;
/** @brief Track if encryption enabled was set from config */
static bool config_encrypt_enabled_set = false;
/** @brief Track if encryption key was set from config */
static bool config_encrypt_key_set = false;
/** @brief Track if password was set from config */
static bool config_password_set = false;
/** @brief Track if keyfile was set from config */
static bool config_encrypt_keyfile_set = false;
/** @brief Track if no_encrypt flag was set from config */
static bool config_no_encrypt_set = false;
/** @brief Track if server key was set from config (client only) */
static bool config_server_key_set = false;
/** @brief Track if client keys were set from config (server only) */
static bool config_client_keys_set = false;

/** @} */

/**
 * @name External Validation Functions
 * @{
 *
 * Forward declarations of validation helper functions from options.c.
 * These functions are used to validate config file values using the same
 * logic as CLI argument validation.
 */
extern int validate_port(const char *value_str, char *error_msg, size_t error_msg_size);
extern int validate_positive_int(const char *value_str, char *error_msg, size_t error_msg_size);
extern int validate_non_negative_int(const char *value_str, char *error_msg, size_t error_msg_size);
extern int validate_color_mode(const char *value_str, char *error_msg, size_t error_msg_size);
extern int validate_render_mode(const char *value_str, char *error_msg, size_t error_msg_size);
extern int validate_palette(const char *value_str, char *error_msg, size_t error_msg_size);
extern int validate_ip_address(const char *value_str, char *parsed_address, size_t address_size, bool is_client,
                               char *error_msg, size_t error_msg_size);
extern float validate_float_non_negative(const char *value_str, char *error_msg, size_t error_msg_size);
extern int validate_fps(const char *value_str, char *error_msg, size_t error_msg_size);

/** @} */

/**
 * @name TOML Value Extraction Helpers
 * @{
 *
 * Helper functions to safely extract typed values from TOML datum structures.
 */

/**
 * @brief Extract string value from TOML datum
 * @param datum TOML datum structure
 * @return Pointer to string value, or NULL if not a string type
 *
 * @ingroup config
 */
static const char *get_toml_string(toml_datum_t datum) {
  if (datum.type == TOML_STRING) {
    return datum.u.s;
  }
  return NULL;
}

/** @} */

/**
 * @name Internal Configuration Application Functions
 * @{
 *
 * These functions apply configuration values from TOML sections to global
 * options, with validation and error handling.
 */

/**
 * @brief Load and apply network configuration from TOML
 * @param toptab Root TOML table
 * @param is_client `true` if loading client config, `false` for server config
 *
 * Applies configuration from the `[network]` section:
 * - `network.port`: Port number (1-65535), can be integer or string (shared between server and client)
 *
 * For client:
 * - `client.address` or `network.address`: Server address to connect to
 *
 * For server:
 * - `server.bind_ipv4`: IPv4 bind address (default: 127.0.0.1)
 * - `server.bind_ipv6`: IPv6 bind address (default: ::1)
 * - `network.address`: Legacy/fallback bind address (used if server.bind_ipv4/bind_ipv6 not set)
 *
 * Invalid values are skipped with warnings.
 *
 * @ingroup config
 */
static void apply_network_config(toml_datum_t toptab, bool is_client) {
  if (is_client) {
    // Client: Get connect address from client.address or network.address (legacy)
    toml_datum_t address = toml_seek(toptab, "client.address");
    const char *address_str = get_toml_string(address);
    if (!address_str || strlen(address_str) == 0) {
      // Fallback to legacy network.address
      toml_datum_t network = toml_seek(toptab, "network");
      if (network.type == TOML_TABLE) {
        address = toml_seek(toptab, "network.address");
        address_str = get_toml_string(address);
      }
    }
    if (address_str && strlen(address_str) > 0 && !config_address_set) {
      char parsed_addr[OPTIONS_BUFF_SIZE];
      char error_msg[256];
      if (validate_ip_address(address_str, parsed_addr, sizeof(parsed_addr), is_client, error_msg, sizeof(error_msg)) ==
          0) {
        SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", parsed_addr);
        config_address_set = true;
      } else {
        CONFIG_WARN("%s (skipping client address)", error_msg);
      }
    }
  } else {
    // Server: Get bind addresses from server.bind_ipv4 and server.bind_ipv6
    toml_datum_t server = toml_seek(toptab, "server");
    if (server.type == TOML_TABLE) {
      // IPv4 bind address - seek from the server table, not root
      toml_datum_t bind_ipv4 = toml_get(server, "bind_ipv4");
      const char *ipv4_str = get_toml_string(bind_ipv4);
      if (ipv4_str && strlen(ipv4_str) > 0 && !config_address_set) {
        char parsed_addr[OPTIONS_BUFF_SIZE];
        char error_msg[256];
        if (validate_ip_address(ipv4_str, parsed_addr, sizeof(parsed_addr), is_client, error_msg, sizeof(error_msg)) ==
            0) {
          SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", parsed_addr);
          config_address_set = true;
        } else {
          const char *errmsg = (strlen(error_msg) > 0) ? error_msg : "Invalid IPv4 address";
          CONFIG_WARN("Invalid server.bind_ipv4 value '%s': %s (skipping)", ipv4_str, errmsg);
        }
      }

      // IPv6 bind address - seek from the server table, not root
      toml_datum_t bind_ipv6 = toml_get(server, "bind_ipv6");
      const char *ipv6_str = get_toml_string(bind_ipv6);
      if (ipv6_str && strlen(ipv6_str) > 0 && !config_address6_set) {
        char parsed_addr[OPTIONS_BUFF_SIZE];
        char error_msg[256];
        if (validate_ip_address(ipv6_str, parsed_addr, sizeof(parsed_addr), is_client, error_msg, sizeof(error_msg)) ==
            0) {
          SAFE_SNPRINTF(opt_address6, OPTIONS_BUFF_SIZE, "%s", parsed_addr);
          config_address6_set = true;
        } else {
          const char *errmsg = (strlen(error_msg) > 0) ? error_msg : "Invalid IPv6 address";
          CONFIG_WARN("Invalid server.bind_ipv6 value '%s': %s (skipping)", ipv6_str, errmsg);
        }
      }
    }

    // Fallback to legacy network.address if no server-specific bind addresses found
    if (!config_address_set) {
      toml_datum_t network = toml_seek(toptab, "network");
      if (network.type == TOML_TABLE) {
        toml_datum_t address = toml_get(network, "address");
        const char *address_str = get_toml_string(address);
        if (address_str && strlen(address_str) > 0) {
          char parsed_addr[OPTIONS_BUFF_SIZE];
          char error_msg[256];
          if (validate_ip_address(address_str, parsed_addr, sizeof(parsed_addr), is_client, error_msg,
                                  sizeof(error_msg)) == 0) {
            SAFE_SNPRINTF(opt_address, OPTIONS_BUFF_SIZE, "%s", parsed_addr);
            config_address_set = true;
          } else {
            CONFIG_WARN("%s (skipping network.address)", error_msg);
          }
        }
      }
    }
  }

  // Port (shared between server and client)
  toml_datum_t network = toml_seek(toptab, "network");
  if (network.type != TOML_TABLE) {
    return; // No network section
  }

  // Port (can be string or integer in TOML)
  toml_datum_t port = toml_seek(toptab, "network.port");
  if (port.type == TOML_STRING && !config_port_set) {
    const char *port_str = port.u.s;
    char error_msg[256];
    if (validate_port(port_str, error_msg, sizeof(error_msg)) == 0) {
      SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "%s", port_str);
      config_port_set = true;
    } else {
      CONFIG_WARN("%s (skipping network.port)", error_msg);
    }
  } else if (port.type == TOML_INT64 && !config_port_set) {
    int64_t port_val = port.u.int64;
    if (port_val >= 1 && port_val <= 65535) {
      SAFE_SNPRINTF(opt_port, OPTIONS_BUFF_SIZE, "%ld", port_val);
      config_port_set = true;
    } else {
      CONFIG_WARN("Invalid port value %lld (must be 1-65535, skipping network.port)", (long long)port_val);
    }
  }
}

/**
 * @brief Load and apply client configuration from TOML
 * @param toptab Root TOML table
 * @param is_client `true` if loading client config, `false` for server config
 *
 * Applies configuration from the `[client]` section. Only processes config
 * when `is_client` is true (server ignores client-specific options).
 *
 * Supported options:
 * - `client.width`: Terminal width (positive integer)
 * - `client.height`: Terminal height (positive integer)
 * - `client.webcam_index`: Webcam device index (non-negative integer)
 * - `client.webcam_flip`: Flip webcam image horizontally (boolean)
 * - `client.color_mode`: Color mode ("none", "16", "256", "truecolor")
 * - `client.render_mode`: Render mode ("half-block", "block", "char")
 * - `client.fps`: Frames per second (1-144), can be integer or string
 * - `client.stretch`: Stretch video to terminal size (boolean)
 * - `client.quiet`: Quiet mode (boolean)
 * - `client.snapshot_mode`: Snapshot mode (boolean)
 * - `client.mirror_mode`: Mirror mode (boolean) - view webcam locally without server
 * - `client.snapshot_delay`: Snapshot delay in seconds (non-negative float)
 *
 * Invalid values are skipped with warnings.
 *
 * @ingroup config
 */
static void apply_client_config(toml_datum_t toptab, bool is_client) {
  if (!is_client) {
    return; // Server doesn't use client config
  }

  toml_datum_t client = toml_seek(toptab, "client");
  if (client.type != TOML_TABLE) {
    return; // No client section
  }

  // Width
  toml_datum_t width = toml_seek(toptab, "client.width");
  if (width.type == TOML_INT64 && !config_width_set && !auto_width) {
    int64_t width_val = width.u.int64;
    if (width_val > 0) {
      opt_width = (unsigned short int)width_val;
      auto_width = false;
      config_width_set = true;
    }
  } else if (width.type == TOML_STRING && !config_width_set) {
    const char *width_str = width.u.s;
    char error_msg[256];
    int width_val = validate_positive_int(width_str, error_msg, sizeof(error_msg));
    if (width_val > 0) {
      opt_width = (unsigned short int)width_val;
      auto_width = false;
      config_width_set = true;
    } else {
      CONFIG_WARN("%s (skipping client.width)", error_msg);
    }
  }

  // Height
  toml_datum_t height = toml_seek(toptab, "client.height");
  if (height.type == TOML_INT64 && !config_height_set && !auto_height) {
    int64_t height_val = height.u.int64;
    if (height_val > 0) {
      opt_height = (unsigned short int)height_val;
      auto_height = false;
      config_height_set = true;
    }
  } else if (height.type == TOML_STRING && !config_height_set) {
    const char *height_str = height.u.s;
    char error_msg[256];
    int height_val = validate_positive_int(height_str, error_msg, sizeof(error_msg));
    if (height_val > 0) {
      opt_height = (unsigned short int)height_val;
      auto_height = false;
      config_height_set = true;
    } else {
      CONFIG_WARN("%s (skipping client.height)", error_msg);
    }
  }

  // Webcam index
  toml_datum_t webcam_index = toml_seek(toptab, "client.webcam_index");
  if (webcam_index.type == TOML_INT64 && !config_webcam_index_set) {
    int64_t idx = webcam_index.u.int64;
    if (idx >= 0) {
      opt_webcam_index = (unsigned short int)idx;
      config_webcam_index_set = true;
    }
  } else if (webcam_index.type == TOML_STRING && !config_webcam_index_set) {
    const char *idx_str = webcam_index.u.s;
    char error_msg[256];
    int idx = validate_non_negative_int(idx_str, error_msg, sizeof(error_msg));
    if (idx >= 0) {
      opt_webcam_index = (unsigned short int)idx;
      config_webcam_index_set = true;
    } else {
      CONFIG_WARN("%s (skipping client.webcam_index)", error_msg);
    }
  }

  // Webcam flip
  toml_datum_t webcam_flip = toml_seek(toptab, "client.webcam_flip");
  if (webcam_flip.type == TOML_BOOLEAN && !config_webcam_flip_set) {
    opt_webcam_flip = webcam_flip.u.boolean;
    config_webcam_flip_set = true;
  }

  // Color mode
  toml_datum_t color_mode = toml_seek(toptab, "client.color_mode");
  const char *color_mode_str = get_toml_string(color_mode);
  if (color_mode_str && !config_color_mode_set) {
    char error_msg[256];
    int mode = validate_color_mode(color_mode_str, error_msg, sizeof(error_msg));
    if (mode >= 0) {
      opt_color_mode = (terminal_color_mode_t)mode;
      config_color_mode_set = true;
    } else {
      CONFIG_WARN("%s (skipping client.color_mode)", error_msg);
    }
  }

  // Render mode
  toml_datum_t render_mode = toml_seek(toptab, "client.render_mode");
  const char *render_mode_str = get_toml_string(render_mode);
  if (render_mode_str && !config_render_mode_set) {
    char error_msg[256];
    int mode = validate_render_mode(render_mode_str, error_msg, sizeof(error_msg));
    if (mode >= 0) {
      opt_render_mode = (render_mode_t)mode;
      config_render_mode_set = true;
    } else {
      CONFIG_WARN("%s (skipping client.render_mode)", error_msg);
    }
  }

  // FPS
  toml_datum_t fps = toml_seek(toptab, "client.fps");
  extern int g_max_fps; // From common.c
  if (fps.type == TOML_INT64) {
    int64_t fps_val = fps.u.int64;
    if (fps_val >= 1 && fps_val <= 144) {
      g_max_fps = (int)fps_val;
    } else {
      CONFIG_WARN("Invalid FPS value %lld (must be 1-144, skipping client.fps)", (long long)fps_val);
    }
  } else if (fps.type == TOML_STRING) {
    const char *fps_str = fps.u.s;
    char error_msg[256];
    int fps_val = validate_fps(fps_str, error_msg, sizeof(error_msg));
    if (fps_val > 0) {
      g_max_fps = fps_val;
    } else {
      CONFIG_WARN("%s (skipping client.fps)", error_msg);
    }
  }

  // Stretch
  toml_datum_t stretch = toml_seek(toptab, "client.stretch");
  if (stretch.type == TOML_BOOLEAN && !config_stretch_set) {
    opt_stretch = stretch.u.boolean ? 1 : 0;
    config_stretch_set = true;
  }

  // Quiet
  toml_datum_t quiet = toml_seek(toptab, "client.quiet");
  if (quiet.type == TOML_BOOLEAN && !config_quiet_set) {
    opt_quiet = quiet.u.boolean ? 1 : 0;
    config_quiet_set = true;
  }

  // Snapshot mode
  toml_datum_t snapshot_mode = toml_seek(toptab, "client.snapshot_mode");
  if (snapshot_mode.type == TOML_BOOLEAN && !config_snapshot_mode_set) {
    opt_snapshot_mode = snapshot_mode.u.boolean ? 1 : 0;
    config_snapshot_mode_set = true;
  }

  // Mirror mode
  toml_datum_t mirror_mode = toml_seek(toptab, "client.mirror_mode");
  if (mirror_mode.type == TOML_BOOLEAN && !config_mirror_mode_set) {
    opt_mirror_mode = mirror_mode.u.boolean ? 1 : 0;
    config_mirror_mode_set = true;
  }

  // Snapshot delay
  toml_datum_t snapshot_delay = toml_seek(toptab, "client.snapshot_delay");
  if (snapshot_delay.type == TOML_FP64 && !config_snapshot_delay_set) {
    double delay = snapshot_delay.u.fp64;
    if (delay >= 0.0) {
      opt_snapshot_delay = (float)delay;
      config_snapshot_delay_set = true;
    } else {
      CONFIG_WARN("Invalid snapshot_delay value %.2f (must be non-negative, skipping)", delay);
    }
  } else if (snapshot_delay.type == TOML_STRING && !config_snapshot_delay_set) {
    const char *delay_str = snapshot_delay.u.s;
    char error_msg[256];
    float delay = validate_float_non_negative(delay_str, error_msg, sizeof(error_msg));
    if (delay >= 0.0f) {
      opt_snapshot_delay = delay;
      config_snapshot_delay_set = true;
    } else {
      CONFIG_WARN("%s (skipping client.snapshot_delay)", error_msg);
    }
  }
}

/**
 * @brief Load and apply audio configuration from TOML
 * @param toptab Root TOML table
 * @param is_client `true` if loading client config, `false` for server config
 *
 * Applies configuration from the `[audio]` section. Only processes config
 * when `is_client` is true (server doesn't use audio options).
 *
 * Supported options:
 * - `audio.enabled`: Enable audio streaming (boolean)
 * - `audio.microphone_index`: Microphone device index (integer, -1 for default)
 * - `audio.speakers_index`: Speakers device index (integer, -1 for default)
 *
 * Invalid values are skipped with warnings.
 *
 * @ingroup config
 */
static void apply_audio_config(toml_datum_t toptab, bool is_client) {
  if (!is_client) {
    return; // Server doesn't use audio config
  }

  toml_datum_t audio = toml_seek(toptab, "audio");
  if (audio.type != TOML_TABLE) {
    return; // No audio section
  }

  // Audio enabled
  toml_datum_t audio_enabled = toml_seek(toptab, "audio.enabled");
  if (audio_enabled.type == TOML_BOOLEAN && !config_audio_enabled_set) {
    opt_audio_enabled = audio_enabled.u.boolean ? 1 : 0;
    config_audio_enabled_set = true;
  }

  // Microphone index
  toml_datum_t microphone_index = toml_seek(toptab, "audio.microphone_index");
  if (microphone_index.type == TOML_INT64 && !config_microphone_index_set) {
    int64_t mic_idx = microphone_index.u.int64;
    if (mic_idx >= -1) {
      opt_microphone_index = (int)mic_idx;
      config_microphone_index_set = true;
    }
  } else if (microphone_index.type == TOML_STRING && !config_microphone_index_set) {
    const char *mic_str = microphone_index.u.s;
    char error_msg[256];
    int mic_idx = validate_non_negative_int(mic_str, error_msg, sizeof(error_msg));
    if (mic_idx >= -1) {
      opt_microphone_index = mic_idx;
      config_microphone_index_set = true;
    } else {
      CONFIG_WARN("%s (skipping audio.microphone_index)", error_msg);
    }
  }

  // Speakers index
  toml_datum_t speakers_index = toml_seek(toptab, "audio.speakers_index");
  if (speakers_index.type == TOML_INT64 && !config_speakers_index_set) {
    int64_t spk_idx = speakers_index.u.int64;
    if (spk_idx >= -1) {
      opt_speakers_index = (int)spk_idx;
      config_speakers_index_set = true;
    }
  } else if (speakers_index.type == TOML_STRING && !config_speakers_index_set) {
    const char *spk_str = speakers_index.u.s;
    char error_msg[256];
    int spk_idx = validate_non_negative_int(spk_str, error_msg, sizeof(error_msg));
    if (spk_idx >= -1) {
      opt_speakers_index = spk_idx;
      config_speakers_index_set = true;
    } else {
      CONFIG_WARN("%s (skipping audio.speakers_index)", error_msg);
    }
  }
}

/**
 * @brief Load and apply palette configuration from TOML
 * @param toptab Root TOML table
 *
 * Applies configuration from the `[palette]` section:
 * - `palette.type`: Palette type ("blocks", "half-blocks", "chars", "custom")
 * - `palette.chars`: Custom palette character string (max 255 chars)
 *
 * If `palette.chars` is provided, `palette.type` is automatically set to "custom".
 * Invalid values are skipped with warnings.
 *
 * @ingroup config
 */
static void apply_palette_config_from_toml(toml_datum_t toptab) {
  toml_datum_t palette = toml_seek(toptab, "palette");
  if (palette.type != TOML_TABLE) {
    return; // No palette section
  }

  // Palette type
  toml_datum_t palette_type = toml_seek(toptab, "palette.type");
  const char *palette_type_str = get_toml_string(palette_type);
  if (palette_type_str && !config_palette_set) {
    char error_msg[256];
    int type = validate_palette(palette_type_str, error_msg, sizeof(error_msg));
    if (type >= 0) {
      opt_palette_type = (palette_type_t)type;
      config_palette_set = true;
    } else {
      CONFIG_WARN("%s (skipping palette.type)", error_msg);
    }
  }

  // Palette chars (custom palette)
  toml_datum_t palette_chars = toml_seek(toptab, "palette.chars");
  const char *palette_chars_str = get_toml_string(palette_chars);
  if (palette_chars_str && !config_palette_chars_set) {
    if (strlen(palette_chars_str) < sizeof(opt_palette_custom)) {
      SAFE_STRNCPY(opt_palette_custom, palette_chars_str, sizeof(opt_palette_custom));
      opt_palette_custom[sizeof(opt_palette_custom) - 1] = '\0';
      opt_palette_custom_set = true;
      opt_palette_type = PALETTE_CUSTOM; // Automatically set to custom
      config_palette_chars_set = true;
    } else {
      CONFIG_WARN("Invalid palette.chars: too long (%zu chars, max %zu, skipping)", strlen(palette_chars_str),
                  sizeof(opt_palette_custom) - 1);
    }
  }
}

/**
 * @brief Load and apply crypto configuration from TOML
 * @param toptab Root TOML table
 * @param is_client `true` if loading client config, `false` for server config
 *
 * Applies configuration from the `[crypto]` section:
 * - `crypto.encrypt_enabled`: Enable encryption (boolean)
 * - `crypto.key`: Encryption key identifier (string, e.g., "gpg:keyid")
 * - `crypto.password`: Password for encryption (string, **warning**: insecure!)
 * - `crypto.keyfile`: Path to key file (string)
 * - `crypto.no_encrypt`: Disable encryption (boolean)
 * - `crypto.server_key`: Server public key (client only, string)
 * - `crypto.client_keys`: Client keys directory (server only, string)
 *
 * @warning Storing passwords in config files is insecure. A warning is printed
 *          if a password is found. Use CLI `--password` or environment variables instead.
 *
 * Invalid values are skipped with warnings.
 *
 * @ingroup config
 */
static asciichat_error_t apply_crypto_config(toml_datum_t toptab, bool is_client) {
  toml_datum_t crypto = toml_seek(toptab, "crypto");
  if (crypto.type != TOML_TABLE) {
    return ASCIICHAT_OK; // No crypto section
  }

  // Encrypt enabled
  toml_datum_t encrypt_enabled = toml_seek(toptab, "crypto.encrypt_enabled");
  if (encrypt_enabled.type == TOML_BOOLEAN && !config_encrypt_enabled_set) {
    opt_encrypt_enabled = encrypt_enabled.u.boolean ? 1 : 0;
    config_encrypt_enabled_set = true;
  }

  // Key
  toml_datum_t key = toml_seek(toptab, "crypto.key");
  const char *key_str = get_toml_string(key);
  if (key_str && strlen(key_str) > 0 && !config_encrypt_key_set) {
    if (path_looks_like_path(key_str)) {
      char *normalized_key = NULL;
      asciichat_error_t key_result = path_validate_user_path(key_str, PATH_ROLE_KEY_PRIVATE, &normalized_key);
      if (key_result != ASCIICHAT_OK) {
        SAFE_FREE(normalized_key);
        return key_result;
      }
      SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", normalized_key);
      SAFE_FREE(normalized_key);
    } else {
      SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", key_str);
    }
    opt_encrypt_enabled = 1; // Auto-enable encryption when key provided
    config_encrypt_key_set = true;
  }

  // Password (WARNING: storing passwords in config file is insecure!)
  // We only load it if explicitly set, but warn user
  toml_datum_t password = toml_seek(toptab, "crypto.password");
  const char *password_str = get_toml_string(password);
  if (password_str && strlen(password_str) > 0 && !config_password_set) {
    size_t password_len = strlen(password_str);
    if (password_len >= MIN_PASSWORD_LENGTH && password_len <= MAX_PASSWORD_LENGTH) {
      CONFIG_WARN("Password stored in config file is insecure! Use CLI --password instead.");
      SAFE_SNPRINTF(opt_password, OPTIONS_BUFF_SIZE, "%s", password_str);
      opt_encrypt_enabled = 1; // Auto-enable encryption when password provided
      config_password_set = true;
    } else {
      CONFIG_WARN("Invalid password length (must be %d-%d, skipping crypto.password)", MIN_PASSWORD_LENGTH,
                  MAX_PASSWORD_LENGTH);
    }
  }

  // Keyfile
  toml_datum_t keyfile = toml_seek(toptab, "crypto.keyfile");
  const char *keyfile_str = get_toml_string(keyfile);
  if (keyfile_str && strlen(keyfile_str) > 0 && !config_encrypt_keyfile_set) {
    if (path_looks_like_path(keyfile_str)) {
      char *normalized_keyfile = NULL;
      asciichat_error_t keyfile_result =
          path_validate_user_path(keyfile_str, PATH_ROLE_KEY_PRIVATE, &normalized_keyfile);
      if (keyfile_result != ASCIICHAT_OK) {
        SAFE_FREE(normalized_keyfile);
        return keyfile_result;
      }
      SAFE_SNPRINTF(opt_encrypt_keyfile, OPTIONS_BUFF_SIZE, "%s", normalized_keyfile);
      SAFE_FREE(normalized_keyfile);
    } else {
      SAFE_SNPRINTF(opt_encrypt_keyfile, OPTIONS_BUFF_SIZE, "%s", keyfile_str);
    }
    opt_encrypt_enabled = 1; // Auto-enable encryption when keyfile provided
    config_encrypt_keyfile_set = true;
  }

  // No encrypt
  toml_datum_t no_encrypt = toml_seek(toptab, "crypto.no_encrypt");
  if (no_encrypt.type == TOML_BOOLEAN && !config_no_encrypt_set) {
    if (no_encrypt.u.boolean) {
      opt_no_encrypt = 1;
      opt_encrypt_enabled = 0;
      config_no_encrypt_set = true;
    }
  }

  // Server key (client only)
  if (is_client) {
    toml_datum_t server_key = toml_seek(toptab, "crypto.server_key");
    const char *server_key_str = get_toml_string(server_key);
    if (server_key_str && strlen(server_key_str) > 0 && !config_server_key_set) {
      if (path_looks_like_path(server_key_str)) {
        char *normalized_server_key = NULL;
        asciichat_error_t server_key_result =
            path_validate_user_path(server_key_str, PATH_ROLE_KEY_PUBLIC, &normalized_server_key);
        if (server_key_result != ASCIICHAT_OK) {
          SAFE_FREE(normalized_server_key);
          return server_key_result;
        }
        SAFE_SNPRINTF(opt_server_key, OPTIONS_BUFF_SIZE, "%s", normalized_server_key);
        SAFE_FREE(normalized_server_key);
      } else {
        SAFE_SNPRINTF(opt_server_key, OPTIONS_BUFF_SIZE, "%s", server_key_str);
      }
      config_server_key_set = true;
    }
  }

  // Client keys (server only)
  if (!is_client) {
    toml_datum_t client_keys = toml_seek(toptab, "crypto.client_keys");
    const char *client_keys_str = get_toml_string(client_keys);
    if (client_keys_str && strlen(client_keys_str) > 0 && !config_client_keys_set) {
      if (path_looks_like_path(client_keys_str)) {
        char *normalized_client_keys = NULL;
        asciichat_error_t client_keys_result =
            path_validate_user_path(client_keys_str, PATH_ROLE_CLIENT_KEYS, &normalized_client_keys);
        if (client_keys_result != ASCIICHAT_OK) {
          SAFE_FREE(normalized_client_keys);
          return client_keys_result;
        }
        SAFE_SNPRINTF(opt_client_keys, OPTIONS_BUFF_SIZE, "%s", normalized_client_keys);
        SAFE_FREE(normalized_client_keys);
      } else {
        SAFE_SNPRINTF(opt_client_keys, OPTIONS_BUFF_SIZE, "%s", client_keys_str);
      }
      config_client_keys_set = true;
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Load and apply log file configuration from TOML
 * @param toptab Root TOML table
 *
 * Applies log file configuration. The log file path can be specified either:
 * - At the root level: `log_file = "path/to/log"`
 * - In a `[logging]` section: `logging.log_file = "path/to/log"`
 *
 * The root-level `log_file` takes precedence if both are present.
 *
 * @ingroup config
 */
static asciichat_error_t apply_log_config(toml_datum_t toptab) {
  // Log file can be in root or in a [logging] section
  toml_datum_t log_file = toml_seek(toptab, "log_file");
  if (log_file.type == TOML_UNKNOWN) {
    log_file = toml_seek(toptab, "logging.log_file");
  }

  const char *log_file_str = get_toml_string(log_file);
  if (log_file_str && strlen(log_file_str) > 0 && !config_log_file_set) {
    char *normalized_log = NULL;
    asciichat_error_t log_result = path_validate_user_path(log_file_str, PATH_ROLE_LOG_FILE, &normalized_log);
    if (log_result != ASCIICHAT_OK) {
      SAFE_FREE(normalized_log);
      return log_result;
    }
    SAFE_SNPRINTF(opt_log_file, OPTIONS_BUFF_SIZE, "%s", normalized_log);
    SAFE_FREE(normalized_log);
    config_log_file_set = true;
  }
  return ASCIICHAT_OK;
}

/** @} */

/**
 * @brief Main function to load configuration from file and apply to global options
 * @param is_client `true` if loading client configuration, `false` for server configuration
 * @param config_path Optional path to config file (NULL uses default location)
 * @param strict If true, errors are fatal; if false, errors are non-fatal warnings
 * @return ASCIICHAT_OK on success, error code on failure (if strict) or non-fatal (if !strict)
 *
 * This is the main entry point for configuration loading. It:
 * 1. Expands the config file path (default location or custom path)
 * 2. Checks if the file exists and is a regular file
 * 3. Parses the TOML file using tomlc17
 * 4. Applies configuration from each section (network, client, palette, crypto, logging)
 * 5. Frees resources and returns
 *
 * Configuration file errors are non-fatal if strict is false:
 * - Missing file: Returns ASCIICHAT_OK (config file is optional)
 * - Not a regular file: Warns and returns ASCIICHAT_OK
 * - Parse errors: Warns and returns ASCIICHAT_OK
 * - Invalid values: Individual values are skipped with warnings
 *
 * If strict is true, any error causes immediate return with error code.
 *
 * @note This function should be called before `options_init()` parses command-line
 *       arguments to ensure CLI arguments can override config file values.
 *
 * @note Configuration warnings are printed to stderr because logging may not be
 *       initialized yet when this function is called.
 *
 * @ingroup config
 */
asciichat_error_t config_load_and_apply(bool is_client, const char *config_path, bool strict) {
  char *config_path_expanded = NULL;
  defer(SAFE_FREE(config_path_expanded));

  if (config_path) {
    // Use custom path provided
    config_path_expanded = expand_path(config_path);
    if (!config_path_expanded) {
      // If expansion fails, try using as-is (might already be absolute)
      config_path_expanded = platform_strdup(config_path);
    }
  } else {
    // Use default location with XDG support
    char *config_dir = get_config_dir();
    defer(SAFE_FREE(config_dir));
    if (config_dir) {
      size_t len = strlen(config_dir) + strlen("config.toml") + 1;
      config_path_expanded = SAFE_MALLOC(len, char *);
      if (config_path_expanded) {
#ifdef _WIN32
        safe_snprintf(config_path_expanded, len, "%sconfig.toml", config_dir);
#else
        safe_snprintf(config_path_expanded, len, "%sconfig.toml", config_dir);
#endif
      }
    }

    // Fallback to ~/.ascii-chat/config.toml
    if (!config_path_expanded) {
      config_path_expanded = expand_path("~/.ascii-chat/config.toml");
    }
  }

  if (!config_path_expanded) {
    if (strict) {
      return SET_ERRNO(ERROR_CONFIG, "Failed to resolve config file path");
    }
    return ASCIICHAT_OK;
  }

  char *validated_config_path = NULL;
  asciichat_error_t validate_result =
      path_validate_user_path(config_path_expanded, PATH_ROLE_CONFIG_FILE, &validated_config_path);
  if (validate_result != ASCIICHAT_OK) {
    return validate_result;
  }
  SAFE_FREE(config_path_expanded);
  config_path_expanded = validated_config_path;

  // Determine display path for error messages (before any early returns)
  const char *display_path = config_path ? config_path : config_path_expanded;

  // Check if config file exists
  struct stat st;
  if (stat(config_path_expanded, &st) != 0) {
    if (strict) {
      return SET_ERRNO(ERROR_CONFIG, "Config file does not exist: '%s'", display_path);
    }
    // File doesn't exist, that's OK - not required (non-strict mode)
    return ASCIICHAT_OK;
  }

  // Verify it's a regular file
  if (!S_ISREG(st.st_mode)) {
    if (strict) {
      return SET_ERRNO(ERROR_CONFIG, "Config file exists but is not a regular file: '%s'", display_path);
    }
    CONFIG_WARN("Config file exists but is not a regular file: '%s' (skipping)", display_path);
    return ASCIICHAT_OK;
  }

  // Parse TOML file
  toml_result_t result = toml_parse_file_ex(config_path_expanded);

  if (!result.ok) {
    // result.errmsg is an array, so check its first character
    const char *errmsg = (strlen(result.errmsg) > 0) ? result.errmsg : "Unknown parse error";

    if (strict) {
      // For strict mode, return detailed error message directly
      // Note: SET_ERRNO stores the message in context, but asciichat_error_string() only returns generic codes
      // So we need to format the error message ourselves here
      char error_buffer[512];
      safe_snprintf(error_buffer, sizeof(error_buffer), "Failed to parse config file '%s': %s", display_path, errmsg);
      return SET_ERRNO(ERROR_CONFIG, "%s", error_buffer);
    }
    CONFIG_WARN("Failed to parse config file '%s': %s (skipping)", display_path, errmsg);
    return ASCIICHAT_OK; // Non-fatal error
  }

  // Apply configuration from each section
  apply_network_config(result.toptab, is_client);
  apply_client_config(result.toptab, is_client);
  apply_audio_config(result.toptab, is_client);
  apply_palette_config_from_toml(result.toptab);
  asciichat_error_t crypto_result = apply_crypto_config(result.toptab, is_client);
  if (crypto_result != ASCIICHAT_OK) {
    return crypto_result;
  }
  asciichat_error_t log_result = apply_log_config(result.toptab);
  if (log_result != ASCIICHAT_OK) {
    return log_result;
  }

  // Reset config flags for next call
  config_address_set = false;
  config_address6_set = false;
  config_port_set = false;

  CONFIG_DEBUG("Loaded configuration from %s", display_path);
  return ASCIICHAT_OK;
}

/**
 * @brief Create default configuration file with all default values
 * @param config_path Path to config file to create (NULL uses default location)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Creates a new configuration file at the specified path (or default location
 * if config_path is NULL) with all configuration options set to their default
 * values from options.h.
 *
 * The created file includes:
 * - Version comment at the top (current ascii-chat version)
 * - All supported configuration sections with default values
 * - Comments explaining each option
 *
 * @note The function will create the directory structure if needed.
 * @note If the file already exists, it will not be overwritten (returns error).
 *
 * @ingroup config
 */
asciichat_error_t config_create_default(const char *config_path) {
  (void)fprintf(stderr, "[DEBUG] config_create_default: called with config_path=%s\n",
                config_path ? config_path : "(NULL)");

  char *config_path_expanded = NULL;
  defer(SAFE_FREE(config_path_expanded));

  if (config_path) {
    // Use custom path provided
    config_path_expanded = expand_path(config_path);
    if (!config_path_expanded) {
      // If expansion fails, try using as-is (might already be absolute)
      config_path_expanded = platform_strdup(config_path);
    }
  } else {
    // Use default location with XDG support
    char *config_dir = get_config_dir();
    defer(SAFE_FREE(config_dir));
    if (config_dir) {
      size_t len = strlen(config_dir) + strlen("config.toml") + 1;
      config_path_expanded = SAFE_MALLOC(len, char *);
      if (config_path_expanded) {
#ifdef _WIN32
        safe_snprintf(config_path_expanded, len, "%sconfig.toml", config_dir);
#else
        safe_snprintf(config_path_expanded, len, "%sconfig.toml", config_dir);
#endif
      }
    }

    // Fallback to ~/.ascii-chat/config.toml
    if (!config_path_expanded) {
      config_path_expanded = expand_path("~/.ascii-chat/config.toml");
    }
  }

  if (!config_path_expanded) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to resolve config file path");
  }

  char *validated_config_path = NULL;
  asciichat_error_t validate_result =
      path_validate_user_path(config_path_expanded, PATH_ROLE_CONFIG_FILE, &validated_config_path);
  if (validate_result != ASCIICHAT_OK) {
    return validate_result;
  }
  SAFE_FREE(config_path_expanded);
  config_path_expanded = validated_config_path;

  // Check if file already exists
  struct stat st;
  if (stat(config_path_expanded, &st) == 0) {
    return SET_ERRNO(ERROR_CONFIG, "Config file already exists: %s", config_path ? config_path : "default location");
  }

  // Create directory if needed
  char *dir_path = platform_strdup(config_path_expanded);
  if (!dir_path) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for directory path");
  }

  // Find the last path separator
  char *last_sep = strrchr(dir_path, PATH_DELIM);

  if (last_sep) {
    *last_sep = '\0';
    // Create directory (similar to known_hosts.c approach)
#ifdef _WIN32
    // Windows: Create directory (creates only one level)
    int mkdir_result = _mkdir(dir_path);
#else
    // POSIX: Create directory with DIR_PERM_PRIVATE permissions
    int mkdir_result = mkdir(dir_path, DIR_PERM_PRIVATE);
#endif
    if (mkdir_result != 0 && errno != EEXIST) {
      // mkdir failed and it's not because the directory already exists
      // Verify if directory actually exists despite the error (Windows compatibility)
      struct stat test_st;
      if (stat(dir_path, &test_st) != 0) {
        // Directory doesn't exist and we couldn't create it
        SAFE_FREE(dir_path);
        return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create config directory: %s", dir_path);
      }
      // Directory exists despite error, proceed
    }
  }
  SAFE_FREE(dir_path);

  // Create file with default values
  FILE *f = platform_fopen(config_path_expanded, "w");
  defer(SAFE_FCLOSE(f));
  if (!f) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create config file: %s", config_path_expanded);
  }

  // Write version comment
  (void)fprintf(f, "# ascii-chat configuration file\n");
  (void)fprintf(f, "# Generated by ascii-chat v%d.%d.%d-%s\n", ASCII_CHAT_VERSION_MAJOR, ASCII_CHAT_VERSION_MINOR,
                ASCII_CHAT_VERSION_PATCH, ASCII_CHAT_GIT_VERSION);
  (void)fprintf(f, "#\n");
  (void)fprintf(f, "# If you upgrade ascii-chat and this version comment changes, you may need to\n");
  (void)fprintf(f, "# delete and regenerate this file with: ascii-chat --config-create\n");
  (void)fprintf(f, "#\n\n");

  // Write network section with defaults
  (void)fprintf(f, "[network]\n");
  (void)fprintf(f, "# Port number (1-65535, shared between server and client)\n");
  // Use opt_port (always initialized to "27224" by default)
  (void)fprintf(f, "#port = %s\n\n", opt_port);

  // Write server section with bind addresses
  (void)fprintf(f, "[server]\n");
  (void)fprintf(f, "# IPv4 bind address (default: 127.0.0.1)\n");
  (void)fprintf(f, "#bind_ipv4 = \"127.0.0.1\"\n");
  (void)fprintf(f, "# IPv6 bind address (default: ::1 for IPv6-only, or :: for dual-stack)\n");
  (void)fprintf(f, "#bind_ipv6 = \"::1\"\n");
  (void)fprintf(f, "# Legacy bind address (fallback if bind_ipv4/bind_ipv6 not set)\n");
  (void)fprintf(f, "#address = \"::\"\n\n");

  // Write client section with defaults
  (void)fprintf(f, "[client]\n");
  (void)fprintf(f, "# Server address to connect to\n");
  (void)fprintf(f, "#address = \"%s\"\n", opt_address);
  (void)fprintf(f, "# Alternative: set via network.address (legacy)\n");
  (void)fprintf(f, "#network.address = \"%s\"\n\n", opt_address);
  (void)fprintf(f, "# Terminal width in characters (0 = auto-detect)\n");
  (void)fprintf(f, "#width = %d\n", OPT_WIDTH_DEFAULT);
  (void)fprintf(f, "# Terminal height in characters (0 = auto-detect)\n");
  (void)fprintf(f, "#height = %d\n", OPT_HEIGHT_DEFAULT);
  (void)fprintf(f, "# Webcam device index (0 = first webcam)\n");
  (void)fprintf(f, "#webcam_index = %hu\n", opt_webcam_index);
  (void)fprintf(f, "# Flip webcam image horizontally\n");
  (void)fprintf(f, "#webcam_flip = %s\n", opt_webcam_flip ? "true" : "false");
  (void)fprintf(f, "# Color mode: \"none\", \"16\", \"256\", \"truecolor\" (or \"auto\" for auto-detect)\n");
  (void)fprintf(f, "#color_mode = \"auto\"\n");
  (void)fprintf(f, "# Render mode: \"foreground\", \"background\", \"half-block\"\n");
  (void)fprintf(f, "#render_mode = \"foreground\"\n");
  (void)fprintf(f, "# Frames per second (1-144, default: 30 for Windows, 60 for Unix)\n");
#if defined(_WIN32)
  (void)fprintf(f, "#fps = 30\n");
#else
  (void)fprintf(f, "#fps = 60\n");
#endif
  (void)fprintf(f, "# Stretch video to terminal size (without preserving aspect ratio)\n");
  (void)fprintf(f, "#stretch = %s\n", opt_stretch ? "true" : "false");
  (void)fprintf(f, "# Quiet mode (disable console logging)\n");
  (void)fprintf(f, "#quiet = %s\n", opt_quiet ? "true" : "false");
  (void)fprintf(f, "# Snapshot mode (capture one frame and exit)\n");
  (void)fprintf(f, "#snapshot_mode = %s\n", opt_snapshot_mode ? "true" : "false");
  (void)fprintf(f, "# Mirror mode (view webcam locally without server)\n");
  (void)fprintf(f, "#mirror_mode = %s\n", opt_mirror_mode ? "true" : "false");
  (void)fprintf(f, "# Snapshot delay in seconds (for webcam warmup)\n");
  (void)fprintf(f, "#snapshot_delay = %.1f\n", (double)opt_snapshot_delay);
  (void)fprintf(f, "# Use test pattern instead of real webcam\n");
  (void)fprintf(f, "#test_pattern = %s\n", opt_test_pattern ? "true" : "false");
  (void)fprintf(f, "# Show terminal capabilities and exit\n");
  (void)fprintf(f, "#show_capabilities = %s\n", opt_show_capabilities ? "true" : "false");
  (void)fprintf(f, "# Force UTF-8 support\n");
  (void)fprintf(f, "#force_utf8 = %s\n\n", opt_force_utf8 ? "true" : "false");

  // Write audio section (client only)
  (void)fprintf(f, "[audio]\n");
  (void)fprintf(f, "# Enable audio streaming\n");
  (void)fprintf(f, "#enabled = %s\n", opt_audio_enabled ? "true" : "false");
  (void)fprintf(f, "# Microphone device index (-1 = use default)\n");
  (void)fprintf(f, "#microphone_index = %d\n", opt_microphone_index);
  (void)fprintf(f, "# Speakers device index (-1 = use default)\n");
  (void)fprintf(f, "#speakers_index = %d\n\n", opt_speakers_index);

  // Write palette section
  (void)fprintf(f, "[palette]\n");
  (void)fprintf(f, "# Palette type: \"blocks\", \"half-blocks\", \"chars\", \"custom\"\n");
  (void)fprintf(f, "#type = \"half-blocks\"\n");
  (void)fprintf(f, "# Custom palette characters (only used if type = \"custom\")\n");
  (void)fprintf(f, "#chars = \"   ...',;:clodxkO0KXNWM\"\n\n");

  // Write crypto section
  (void)fprintf(f, "[crypto]\n");
  (void)fprintf(f, "# Enable encryption\n");
  (void)fprintf(f, "#encrypt_enabled = %s\n", opt_encrypt_enabled ? "true" : "false");
  (void)fprintf(f, "# Encryption key identifier (e.g., \"gpg:keyid\" or \"github:username\")\n");
  (void)fprintf(f, "#key = \"%s\"\n", opt_encrypt_key);
  (void)fprintf(f, "# Password for encryption (WARNING: storing passwords in config files is insecure!)\n");
  (void)fprintf(f, "# Use CLI --password or environment variables instead.\n");
  (void)fprintf(f, "#password = \"%s\"\n", opt_password);
  (void)fprintf(f, "# Key file path\n");
  (void)fprintf(f, "#keyfile = \"%s\"\n", opt_encrypt_keyfile);
  (void)fprintf(f, "# Disable encryption (opt-out)\n");
  (void)fprintf(f, "#no_encrypt = %s\n", opt_no_encrypt ? "true" : "false");
  (void)fprintf(f, "# Server public key (client only)\n");
  (void)fprintf(f, "#server_key = \"%s\"\n", opt_server_key);
  (void)fprintf(f, "# Client keys directory (server only)\n");
  (void)fprintf(f, "#client_keys = \"%s\"\n\n", opt_client_keys);

  // Write logging section
  (void)fprintf(f, "[logging]\n");
  (void)fprintf(f, "# Log file path (empty string = no file logging)\n");
  (void)fprintf(f, "#log_file = \"%s\"\n", opt_log_file);

  return ASCIICHAT_OK;
}
