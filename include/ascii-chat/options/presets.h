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
options_config_t *options_preset_unified(const char *program_name, const char *description);

/** @} */
