/** Browser-facing C interface implemented in browser_discovery.c. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct acds_protocol acds_protocol_t;

#ifdef __cplusplus
extern "C" {
#endif

acds_protocol_t *browser_discovery_new(void);
void browser_discovery_destroy(acds_protocol_t *p);
asciichat_error_t browser_discovery_enter(acds_protocol_t *p, bool create, const char *name, const char *password);
asciichat_error_t browser_discovery_receive(acds_protocol_t *p, uint32_t type, const void *data, size_t length);
asciichat_error_t browser_discovery_leave(acds_protocol_t *p);
void browser_discovery_snapshot(acds_protocol_t *p);
void browser_handoff_encode(int kind, const uint8_t *session, const uint8_t *id, uint32_t generation, uint32_t sequence,
                            int capabilities, int nat_tier, int port, uint8_t *out);
asciichat_error_t browser_handoff_decode(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif
