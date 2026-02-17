/**
 * @file mode_defaults.h
 * @brief Mode-aware default value getters for options
 * @ingroup options
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <ascii-chat/options/options.h>

/**
 * @brief Get mode-specific default value for --log-format
 * @param mode The mode to get the default for
 * @return Pointer to the default log format string for this mode
 */
const void *get_default_log_format(asciichat_mode_t mode);

/**
 * @brief Get mode-specific default value for --log-file
 * @param mode The mode to get the default for
 * @return Pointer to the default log file path for this mode
 */
const void *get_default_log_file(asciichat_mode_t mode);

/**
 * @brief Get mode-specific default value for --port
 * @param mode The mode to get the default for
 * @return Pointer to the default port number for this mode
 */
const void *get_default_port(asciichat_mode_t mode);

/**
 * @brief Get mode-specific default value for --websocket-port
 * @param mode The mode to get the default for
 * @return Pointer to the default WebSocket port number for this mode
 */
const void *get_default_websocket_port(asciichat_mode_t mode);

/**
 * @brief Apply mode-specific defaults to an options struct after mode detection
 *
 * Updates options that have mode-dependent defaults (like log_file, port, and
 * websocket_port) based on the detected mode. Should be called after mode
 * detection but before option parsing.
 *
 * @param opts Options struct to update (must have detected_mode already set)
 */
void apply_mode_specific_defaults(options_t *opts);
