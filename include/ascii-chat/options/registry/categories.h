/**
 * @file categories.h
 * @brief Extern declarations for all category entry arrays
 * @ingroup options
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "common.h"

// Category arrays - each file contains options from only ONE help group
extern const registry_entry_t g_general_entries[];
extern const registry_entry_t g_logging_entries[];
extern const registry_entry_t g_terminal_entries[];
extern const registry_entry_t g_configuration_entries[];
extern const registry_entry_t g_display_entries[];
extern const registry_entry_t g_webcam_entries[];
extern const registry_entry_t g_audio_entries[];
extern const registry_entry_t g_media_entries[];
extern const registry_entry_t g_network_entries[];
extern const registry_entry_t g_security_entries[];
extern const registry_entry_t g_database_entries[];

#ifndef NDEBUG
extern const registry_entry_t g_debug_entries[];
#endif
