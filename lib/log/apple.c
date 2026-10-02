/**
 * @file log/apple.c
 * @brief Apple unified logging (os_log) integration
 * @ingroup logging
 *
 * Routes ascii-chat log messages to Apple's unified logging system via os_log.
 * Messages appear in Console.app and are queryable with the `log` CLI tool:
 *
 *   log stream --predicate 'subsystem == "chat.ascii.ascii-chat"' --level debug
 */

#include <os/log.h>
#include <string.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/string.h>

#define APPLE_LOG_SUBSYSTEM "com.ascii-chat"
#define APPLE_LOG_CATEGORY  "default"

static os_log_t apple_log = NULL;

static os_log_t get_apple_log(void) {
    if (!apple_log) {
        apple_log = os_log_create(APPLE_LOG_SUBSYSTEM, APPLE_LOG_CATEGORY);
    }
    return apple_log;
}

static os_log_type_t level_to_os_log_type(log_level_t level) {
    switch (level) {
    case LOG_DEV:
    case LOG_DEBUG:
        return OS_LOG_TYPE_DEBUG;
    case LOG_INFO:
        return OS_LOG_TYPE_INFO;
    case LOG_WARN:
        // OS_LOG_TYPE_DEFAULT is the closest to "warning" in os_log;
        // os_log has no dedicated warning level.
        return OS_LOG_TYPE_DEFAULT;
    case LOG_ERROR:
        return OS_LOG_TYPE_ERROR;
    case LOG_FATAL:
        return OS_LOG_TYPE_FAULT;
    default:
        return OS_LOG_TYPE_DEFAULT;
    }
}

// Platform hook called by the logging system after each message is formatted.
// The message contains the full formatted log line with ANSI color codes.
// We strip ANSI codes before sending to os_log for clean output.
void platform_log_hook(log_level_t level, const char *message) {
    if (!message) {
        return;
    }

    os_log_t log = get_apple_log();

    // Strip ANSI escape codes for clean system log output
    char clean[4096];
    strip_ansi_codes(message, clean, sizeof(clean));

    os_log_with_type(log, level_to_os_log_type(level), "%{public}s", clean);
}
