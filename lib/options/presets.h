/**
 * @file presets.h
 * @brief Preset option configurations for ascii-chat modes
 * @ingroup options
 */

#pragma once

#include "builder.h"

/**
 * @brief Build unified options config with ALL options (binary + all modes)
 *
 * This is the single source of truth for all options. Each option has a
 * mode_bitmask indicating which modes it applies to. The config includes
 * all options, and validation happens after parsing based on detected mode.
 */
const options_config_t *options_preset_unified(const char *program_name, const char *description);

/**
 * @brief Create binary-level options preset (DEPRECATED - use options_preset_unified)
 */
const options_config_t *options_preset_binary(const char *program_name, const char *description);

/**
 * @brief Create server mode options preset
 */
const options_config_t *options_preset_server(const char *program_name, const char *description);

/**
 * @brief Create client mode options preset
 */
const options_config_t *options_preset_client(const char *program_name, const char *description);

/**
 * @brief Create mirror mode options preset
 */
const options_config_t *options_preset_mirror(const char *program_name, const char *description);

/**
 * @brief Create ACDS (discovery service) mode options preset
 */
const options_config_t *options_preset_acds(const char *program_name, const char *description);

/**
 * @brief Create discovery mode options preset
 */
const options_config_t *options_preset_discovery(const char *program_name, const char *description);

/**
 * @brief Add binary-level logging options to a builder
 */
void options_builder_add_logging_group(options_builder_t *b);

/**
 * @brief Add terminal dimension options (width, height)
 */
void options_builder_add_terminal_group(options_builder_t *b);

/**
 * @brief Add webcam options
 */
void options_builder_add_webcam_group(options_builder_t *b);

/**
 * @brief Add display/rendering options
 */
void options_builder_add_display_group(options_builder_t *b);

/**
 * @brief Add snapshot mode options
 */
void options_builder_add_snapshot_group(options_builder_t *b);

/**
 * @brief Add compression options
 */
void options_builder_add_compression_group(options_builder_t *b);

/**
 * @brief Add cryptography/security options
 */
void options_builder_add_crypto_group(options_builder_t *b);

/**
 * @brief Add port option
 */
void options_builder_add_port_option(options_builder_t *b, const char *default_port, const char *env_var);

/**
 * @brief Add ACDS discovery options
 */
void options_builder_add_acds_group(options_builder_t *b);

/** @} */
