/**
 * @file registry/debug.c
 * @brief Debug options registry (backtrace, sync-state)
 * @ingroup options
 *
 * Debug-only options for development and troubleshooting.
 */

#include <ascii-chat/options/registry/common.h>

#ifndef NDEBUG

// clang-format off
const registry_entry_t g_debug_entries[] = {
    {"sync-state",
     '\0',
     OPTION_TYPE_DOUBLE,
     offsetof(options_t, debug_sync_state_time),
     &default_debug_sync_state_time_value,
     sizeof(double),
     "Print synchronization primitive state with optional time offset (debug builds only).",
     "DEBUG",
     "TIME",
     false,  // required
     NULL,   // env_var_name
     NULL,   // validate_fn
     NULL,   // parse_fn
     false,  // owns_memory
     true,   // optional_arg
     OPTION_MODE_ALL,
     {0},    // metadata
     NULL},  // action_fn
    {"backtrace",
     '\0',
     OPTION_TYPE_DOUBLE,
     offsetof(options_t, debug_backtrace_time),
     &default_debug_backtrace_time_value,
     sizeof(double),
     "Print backtrace with optional time offset (debug builds only).",
     "DEBUG",
     "TIME",
     false,  // required
     NULL,   // env_var_name
     NULL,   // validate_fn
     NULL,   // parse_fn
     false,  // owns_memory
     true,   // optional_arg
     OPTION_MODE_ALL,
     {0},    // metadata
     NULL},  // action_fn

    REGISTRY_TERMINATOR()};
// clang-format on

#else

// Empty registry for non-debug builds
const registry_entry_t g_debug_entries[] = {REGISTRY_TERMINATOR()};

#endif
