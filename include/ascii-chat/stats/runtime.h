#pragma once

/* Storage capacities in elements; text buffers include the terminating NUL. */
#define STATS_RUNTIME_MODE_BUFFER_SIZE 32

/* Existing buffer budgets in bytes, including NUL for text storage. */
#define STATS_VIEW_LINE_BUFFER_SIZE 160

#include <ascii-chat/stats/stats.h>
#include <stddef.h>

typedef struct stats_peer stats_peer_t;
#define STATS_VIEW_LINES 128
typedef struct {
  char mode[STATS_RUNTIME_MODE_BUFFER_SIZE];
  char lines[STATS_VIEW_LINES][STATS_VIEW_LINE_BUFFER_SIZE];
  bool headings[STATS_VIEW_LINES];
  unsigned count;
  unsigned offset;
  unsigned page;
  unsigned selected;
  uint64_t sampled_ns;
/**
 * @brief stats view is intentionally a small, concrete record. Fields such as `count`, `offset`, `page`, `selected` give callers the identity and current value they need. It is normally borrowed from, or owned by, the surrounding subsystem—not independently managed.
 */
} stats_view_t;

/** Provider runs on the stats worker; unregister before destroying its data.
 * Never call stats_runtime_set_provider() from a provider. */
typedef void (*stats_provider_fn)(stats_view_t *view, void *data);
asciichat_error_t stats_runtime_start(const char *mode, unsigned interval_seconds);
void stats_runtime_stop(void);
stats_scope_t *stats_runtime_scope(void);
void stats_runtime_set_provider(stats_provider_fn provider, void *data);
void stats_view_add(stats_view_t *view, const char *format, ...);
void stats_view_add_header(stats_view_t *view, const char *format, ...);
void stats_runtime_toggle(void);
bool stats_runtime_active(void);
stats_peer_t *stats_runtime_peer_open(const char *transport);
void stats_runtime_peer_close(stats_peer_t *peer);
stats_scope_t *stats_runtime_peer_scope(stats_peer_t *peer);
/** Application packet boundary, after decryption on receive. Header bytes are included internally. */
void stats_runtime_packet(stats_peer_t *peer, unsigned type, size_t payload_bytes, bool sent, bool success);
void stats_runtime_connection_state(const char *state);
void stats_runtime_media(unsigned width, unsigned height, double position, double duration);
