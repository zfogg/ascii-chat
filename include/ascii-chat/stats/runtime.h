#pragma once
#include <ascii-chat/stats/stats.h>
#include <stddef.h>

typedef struct stats_peer stats_peer_t;
#define STATS_VIEW_LINES 128
typedef struct {
  char mode[32];
  char lines[STATS_VIEW_LINES][160];
  unsigned count;
  unsigned offset;
  unsigned page;
  unsigned selected;
  uint64_t sampled_ns;
} stats_view_t;

/** Provider runs on the stats worker; unregister before destroying its data.
 * Never call stats_runtime_set_provider() from a provider. */
typedef void (*stats_provider_fn)(stats_view_t *view, void *data);
asciichat_error_t stats_runtime_start(const char *mode, unsigned interval_seconds);
void stats_runtime_stop(void);
stats_scope_t *stats_runtime_scope(void);
void stats_runtime_set_provider(stats_provider_fn provider, void *data);
void stats_view_add(stats_view_t *view, const char *format, ...);
void stats_runtime_toggle(void);
bool stats_runtime_active(void);
stats_peer_t *stats_runtime_peer_open(const char *transport);
void stats_runtime_peer_close(stats_peer_t *peer);
stats_scope_t *stats_runtime_peer_scope(stats_peer_t *peer);
/** Application packet boundary, after decryption on receive. Header bytes are included internally. */
void stats_runtime_packet(stats_peer_t *peer, unsigned type, size_t payload_bytes, bool sent, bool success);
void stats_runtime_connection_state(const char *state);
void stats_runtime_media(unsigned width, unsigned height, double position, double duration);
