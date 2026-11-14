#pragma once

#include <libplatform/wakeup.h>

#if 0
#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#endif

#if 0
typedef struct platform_wakeup platform_wakeup_t;

/** Coalescing notification for a single consumer. Publish work before signal;
 * after wait returns, inspect the work even if the wait timed out. Join all
 * users before destroying the notification. */
asciichat_error_t platform_wakeup_create(platform_wakeup_t **out);
void platform_wakeup_signal(platform_wakeup_t *wake);
bool platform_wakeup_wait(platform_wakeup_t *wake, uint64_t timeout_ns);
void platform_wakeup_destroy(platform_wakeup_t *wake);

#ifdef __cplusplus
}
#endif

#endif
