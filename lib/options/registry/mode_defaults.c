/**
 * @file mode_defaults.c
 * @brief Mode-aware default value getters for options
 * @ingroup options
 *
 * Provides mode-specific default values for options that vary by mode.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <ascii-chat/options/registry/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/system.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Mode-Aware Default: log-format
// ============================================================================

/**
 * @brief Get mode-specific default value for --log-format
 *
 * Returns the default log format string for all modes.
 */
const void *get_default_log_format(asciichat_mode_t mode) {
  (void)mode; /* All modes use the same default format string */
  return OPT_LOG_FORMAT_DEFAULT;
}

// ============================================================================
// Mode-Aware Default: log-file
// ============================================================================

static char g_log_file_server[OPTIONS_BUFF_SIZE] = {0};
static char g_log_file_client[OPTIONS_BUFF_SIZE] = {0};
static char g_log_file_mirror[OPTIONS_BUFF_SIZE] = {0};
static char g_log_file_discovery_service[OPTIONS_BUFF_SIZE] = {0};
static char g_log_file_discovery[OPTIONS_BUFF_SIZE] = {0};
static bool g_log_file_defaults_initialized = false;

/**
 * @brief Initialize log file default paths using temp directory
 */
static void init_log_file_defaults(void) {
  if (g_log_file_defaults_initialized) {
    return;
  }

  char temp_dir[PLATFORM_MAX_PATH_LENGTH];
  if (!platform_get_temp_dir(temp_dir, sizeof(temp_dir))) {
    // Fallback for POSIX systems
    snprintf(temp_dir, sizeof(temp_dir), "/tmp");
  }

  snprintf(g_log_file_server, sizeof(g_log_file_server), "%s/ascii-chat-server.log", temp_dir);
  snprintf(g_log_file_client, sizeof(g_log_file_client), "%s/ascii-chat-client.log", temp_dir);
  snprintf(g_log_file_mirror, sizeof(g_log_file_mirror), "%s/ascii-chat-mirror.log", temp_dir);
  snprintf(g_log_file_discovery_service, sizeof(g_log_file_discovery_service), "%s/ascii-chat-discovery-service.log",
           temp_dir);
  snprintf(g_log_file_discovery, sizeof(g_log_file_discovery), "%s/ascii-chat.log", temp_dir);

  g_log_file_defaults_initialized = true;
}

/**
 * @brief Get mode-specific default value for --log-file
 */
const void *get_default_log_file(asciichat_mode_t mode) {
  init_log_file_defaults();

  switch (mode) {
  case MODE_SERVER:
    return g_log_file_server;
  case MODE_CLIENT:
    return g_log_file_client;
  case MODE_MIRROR:
    return g_log_file_mirror;
  case MODE_DISCOVERY_SERVICE:
    return g_log_file_discovery_service;
  case MODE_DISCOVERY:
    return g_log_file_discovery;
  case MODE_INVALID:
  default:
    // Fallback for binary-level (before mode detection)
    return "";
  }
}

// ============================================================================
// Mode-Aware Default: port
// ============================================================================

static const int g_port_server = OPT_PORT_INT_DEFAULT;                 // 27224
static const int g_port_client = OPT_PORT_INT_DEFAULT;                 // 27224
static const int g_port_discovery_service = OPT_ACDS_PORT_INT_DEFAULT; // 27225
static const int g_port_discovery = OPT_PORT_INT_DEFAULT;              // 27224 (connects via discovery)

/**
 * @brief Get mode-specific default value for --port
 */
const void *get_default_port(asciichat_mode_t mode) {
  switch (mode) {
  case MODE_SERVER:
    return &g_port_server;
  case MODE_CLIENT:
    return &g_port_client;
  case MODE_DISCOVERY_SERVICE:
    return &g_port_discovery_service;
  case MODE_DISCOVERY:
    return &g_port_discovery;
  case MODE_MIRROR:
  case MODE_INVALID:
  default:
    // Fallback
    return &g_port_server;
  }
}

// ============================================================================
// Mode-Aware Default: websocket-port
// ============================================================================

static const int g_websocket_port_server = OPT_WEBSOCKET_PORT_SERVER_DEFAULT;          // 27226
static const int g_websocket_port_discovery_service = OPT_WEBSOCKET_PORT_ACDS_DEFAULT; // 27227

/**
 * @brief Get mode-specific default value for --websocket-port
 */
const void *get_default_websocket_port(asciichat_mode_t mode) {
  switch (mode) {
  case MODE_SERVER:
    return &g_websocket_port_server;
  case MODE_DISCOVERY_SERVICE:
    return &g_websocket_port_discovery_service;
  case MODE_CLIENT:
  case MODE_MIRROR:
  case MODE_DISCOVERY:
  case MODE_INVALID:
  default:
    // Fallback (should not be used for client/mirror/discovery)
    return &g_websocket_port_server;
  }
}

// ============================================================================
// Mode-Aware Default Application
// ============================================================================

/**
 * @brief Apply mode-specific defaults to an options struct after mode detection
 *
 * This function updates options that have mode-dependent defaults (port and
 * websocket_port) based on the detected mode. Log file paths are handled
 * separately by options_get_log_filepath().
 *
 * @param opts Options struct to update (must have detected_mode already set)
 */
void apply_mode_specific_defaults(options_t *opts) {
  if (!opts) {
    return;
  }

  // Apply port default based on mode
  const int *mode_port = (const int *)get_default_port(opts->detected_mode);
  if (mode_port) {
    // Only update if still at a generic default (27224 or 27225)
    if (opts->port == OPT_PORT_INT_DEFAULT || opts->port == OPT_ACDS_PORT_INT_DEFAULT) {
      opts->port = *mode_port;
    }
  }

  // Apply websocket-port default based on mode
  const int *mode_websocket_port = (const int *)get_default_websocket_port(opts->detected_mode);
  if (mode_websocket_port) {
    // Only update if still at a generic default (27226 or 27227)
    if (opts->websocket_port == OPT_WEBSOCKET_PORT_SERVER_DEFAULT ||
        opts->websocket_port == OPT_WEBSOCKET_PORT_ACDS_DEFAULT) {
      opts->websocket_port = *mode_websocket_port;
    }
  }
}
