/** Browser-facing C interface implemented in session_host.c. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

#ifdef __cplusplus
extern "C" {
#endif

asciichat_error_t browser_host_receive(uint32_t id, uint32_t type, const void *data, size_t length);

asciichat_error_t browser_host_create(void);

void browser_host_destroy(void);

uint32_t browser_host_add_client(void);

asciichat_error_t browser_host_set_local_client(uint32_t id);

void browser_host_remove_client(uint32_t id);

asciichat_error_t browser_host_render(void);

asciichat_error_t browser_host_render_audio(void);

#ifdef __cplusplus
}
#endif
