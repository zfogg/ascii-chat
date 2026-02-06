/**
 * @file core.h
 * @brief Internal helper functions and data for registry implementation
 * @ingroup options
 *
 * This header declares internal helper functions and global registry data
 * that are used by core.c and public_api.c. These are NOT part of the public API.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "common.h"
#include <ascii-chat/common.h>
#include <ascii-chat/options/builder.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// Global Registry Data (defined in registry.c)
// ============================================================================

// Unified view of all registry entries
extern registry_entry_t g_options_registry[2048];
extern size_t g_registry_size;
extern bool g_metadata_populated;

// Category builders array - maps categories to their entry arrays
extern category_builder_t g_category_builders[];

// ============================================================================
// Internal Helper Functions (implemented in core.c)
// ============================================================================

void registry_init_from_builders(void);
asciichat_error_t registry_validate_unique_options(void);
void registry_init_size(void);
const registry_entry_t *registry_find_entry_by_name(const char *long_name);
const registry_entry_t *registry_find_entry_by_short(char short_name);
option_descriptor_t registry_entry_to_descriptor(const registry_entry_t *entry);
bool registry_entry_applies_to_mode(const registry_entry_t *entry, asciichat_mode_t mode, bool for_binary_help);
