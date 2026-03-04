/**
 * @file mode_defaults.h
 * @brief Mode-aware default value getters and mode metadata
 * @ingroup options
 *
 * Provides mode-specific default values and metadata (names, descriptions, groups)
 * for all application modes. Used by option parsing and completion generation.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <ascii-chat/options/options.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Metadata for a mode
 *
 * Describes a mode including its name, user-friendly description,
 * and category grouping (server-like or client-like).
 */
typedef struct {
  asciichat_mode_t mode;
  const char *name;        // "server", "client", etc.
  const char *description; // User-friendly description
  const char *group;       // "server-like" or "client-like"
} mode_descriptor_t;

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

/**
 * @brief Get mode descriptor by mode
 *
 * Returns metadata (name, description, group) for the given mode.
 * Useful for UI generation and help text.
 *
 * @param mode Mode to look up
 * @return Pointer to mode descriptor, or NULL if mode not found
 *
 * @par Example
 * @code
 * const mode_descriptor_t *desc = get_mode_descriptor(MODE_SERVER);
 * if (desc) {
 *     printf("Mode: %s\nDescription: %s\nGroup: %s\n",
 *            desc->name, desc->description, desc->group);
 * }
 * @endcode
 */
const mode_descriptor_t *get_mode_descriptor(asciichat_mode_t mode);

/**
 * @brief Get all mode descriptors for a specific group
 *
 * Returns all modes in a given category (e.g., "server-like" or "client-like").
 * The returned array is allocated with SAFE_MALLOC and must be freed by caller.
 *
 * @param group Group name ("server-like" or "client-like")
 * @param out_count OUTPUT: Number of modes in this group
 * @return Array of mode descriptors (caller must free with SAFE_FREE), or NULL if group not found
 *
 * @par Example
 * @code
 * size_t count;
 * const mode_descriptor_t *server_modes = get_modes_by_group("server-like", &count);
 * if (server_modes) {
 *     for (size_t i = 0; i < count; i++) {
 *         printf("%s: %s\n", server_modes[i].name, server_modes[i].description);
 *     }
 *     SAFE_FREE(server_modes);
 * }
 * @endcode
 */
const mode_descriptor_t *get_modes_by_group(const char *group, size_t *out_count);

#ifdef __cplusplus
}
#endif
