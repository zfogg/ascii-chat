/* Browser presentation is owned by JavaScript; the native terminal runtime is unavailable. */
#include <ascii-chat/stats/runtime.h>

asciichat_error_t stats_runtime_start(const char *mode, unsigned interval_seconds) {
  (void)mode;
  (void)interval_seconds;
  return SET_ERRNO(ERROR_INVALID_STATE, "Native statistics runtime is unavailable in browsers");
}
void stats_runtime_stop(void) {}
stats_scope_t *stats_runtime_scope(void) {
  return NULL;
}
stats_scope_t *stats_runtime_peer_scope(stats_peer_t *peer) {
  (void)peer;
  return NULL;
}
void stats_runtime_set_provider(stats_provider_fn provider, void *data) {
  (void)provider;
  (void)data;
}
void stats_view_add(stats_view_t *view, const char *format, ...) {
  (void)view;
  (void)format;
}
void stats_runtime_toggle(void) {}
bool stats_runtime_active(void) {
  return false;
}
stats_peer_t *stats_runtime_peer_open(const char *transport) {
  (void)transport;
  return NULL;
}
void stats_runtime_peer_close(stats_peer_t *peer) {
  (void)peer;
}
void stats_runtime_packet(stats_peer_t *peer, unsigned type, size_t bytes, bool sent, bool success) {
  (void)peer;
  (void)type;
  (void)bytes;
  (void)sent;
  (void)success;
}
void stats_runtime_connection_state(const char *state) {
  (void)state;
}
void stats_runtime_media(unsigned width, unsigned height, double position, double duration) {
  (void)width;
  (void)height;
  (void)position;
  (void)duration;
}
