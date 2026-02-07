/**
 * @file registry.c
 * @brief Master registry composition - combines all category arrays
 * @ingroup options
 *
 * This file contains the master registry that combines all category-specific
 * option arrays into a single unified registry. It does not implement any
 * logic, only data composition.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/options/registry/common.h>
#include <ascii-chat/options/registry/categories.h>
#include <ascii-chat/options/registry/core.h>
#include <string.h>

// ============================================================================
// Master Registry - Composition of all category arrays
// ============================================================================
// Array of pointers to each category's entries for organized access
// Note: count field is computed at runtime (sentinel-terminated arrays)
// Non-static so core.c can access it via extern declaration
// Each category file contains options from ONLY ONE help group
category_builder_t g_category_builders[] = {
    {g_general_entries, "GENERAL"},   {g_logging_entries, "LOGGING"},
    {g_terminal_entries, "TERMINAL"}, {g_configuration_entries, "CONFIGURATION"},
    {g_display_entries, "DISPLAY"},   {g_webcam_entries, "WEBCAM"},
    {g_audio_entries, "AUDIO"},       {g_media_entries, "MEDIA"},
    {g_network_entries, "NETWORK"},   {g_security_entries, "SECURITY"},
    {g_database_entries, "DATABASE"}, {NULL, NULL}};

// Unified view of all registry entries (for backward compatibility)
registry_entry_t g_options_registry[2048];

size_t g_registry_size = 0;
bool g_metadata_populated = false;

// Metadata is now initialized compile-time in registry entries
