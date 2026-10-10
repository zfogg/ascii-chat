/**
 * @file registry/debug.c
 * @brief Debug options registry (backtrace, sync-state)
 * @ingroup options
 *
 * Debug-only options for development and troubleshooting.
 */

#include <ascii-chat/options/registry/common.h>


// clang-format off
const registry_entry_t g_debug_entries[] = {
    {"sync-state",
     '\0',
     OPTION_TYPE_DOUBLE,
     offsetof(options_t, debug_sync_state_time),
     &default_debug_sync_state_time_value,
     sizeof(double),
     "Print synchronization state and named registry statistics after TIME seconds.",
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
    {"errno-stacks",
     '\0',
     OPTION_TYPE_DOUBLE,
     offsetof(options_t, debug_errno_stacks_time),
     &default_debug_errno_stacks_time_value,
     sizeof(double),
     "Print pending errors, handled history, and registry statistics after TIME seconds.",
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
#ifndef NDEBUG
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
    {"memory-report",
     '\0',
     OPTION_TYPE_DOUBLE,
     offsetof(options_t, debug_memory_report_interval),
     &default_debug_memory_report_interval_value,
     sizeof(double),
     "Print memory report periodically at specified interval in seconds (debug builds only).",
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

#endif
    REGISTRY_TERMINATOR()};
// clang-format on
