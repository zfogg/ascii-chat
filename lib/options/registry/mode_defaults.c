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
// Mode-Aware Default: log-file
// ============================================================================

/**
 * @brief Get mode-specific default value for --log-file
 *
 * Returns empty string - the actual log file path is determined by
 * options_get_log_filepath() which properly handles debug vs release
 * builds and platform-specific temp directories.
 *
 * Debug builds: Uses current working directory (server.log, client.log, etc.)
 * Release builds: Uses $TMPDIR/ascii-chat/server.log, etc.
 */
const void *get_default_log_file(asciichat_mode_t mode) {
  (void)mode; // Unused
  // Return empty string - let options_get_log_filepath() handle the defaults
  return "";
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
