/**
 * @file database.c
 * @brief Discovery service database options
 * @ingroup options
 *
 * Options for discovery service database configuration.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/options/registry/common.h>

// ============================================================================
// DATABASE CATEGORY - Discovery service database options
// ============================================================================
const registry_entry_t g_database_entries[] = {
    // DATABASE GROUP - ACDS Server Specific Options
    {"database",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, discovery_database_path),
     "",
     0,
     "Path to SQLite database for discovery session storage.",
     "DATABASE",
     NULL,
     false,
     "ASCII_CHAT_DATABASE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_DISCOVERY_SVC,
     {0}},

    REGISTRY_TERMINATOR()};
