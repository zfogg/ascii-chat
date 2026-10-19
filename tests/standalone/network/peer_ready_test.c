/* Exercise production readiness dispatch without external ICE services. */
#include "../../../lib/network/webrtc/peer_manager.c"
#include <stdlib.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expr);                                                   \
  } while (0)
static thread_id_t controlling_thread;
static unsigned callbacks, released;
static void destroy_transport(acip_transport_t *transport) {
  (void)transport;
  released++;
}
static const acip_transport_methods_t methods = {.destroy_impl = destroy_transport};
static void ready(acip_transport_t *transport, const uint8_t id[16], void *context) {
  CHECK(context && asciichat_thread_equal(controlling_thread, asciichat_thread_self()));
  CHECK(id[0] == 1 || id[0] == 2);
  callbacks++;
  acip_transport_destroy(transport);
}
static peer_entry_t *add_peer(webrtc_peer_manager_t *manager, uint8_t id) {
  peer_entry_t *peer = SAFE_CALLOC(1, sizeof(*peer), peer_entry_t *);
  peer->manager = manager;
  peer->participant_id[0] = id;
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(*transport), acip_transport_t *);
  transport->methods = &methods;
  atomic_ptr_store(&peer->pending_transport, transport);
  HASH_ADD(hh, manager->peers, participant_id, 16, peer);
  return peer;
}
static void *publish_ready(void *context) {
  peer_entry_t *peer = context;
  on_peer_security_ready(atomic_ptr_load(&peer->pending_transport), peer);
  return NULL;
}
int main(void) {
  log_init(NULL, LOG_WARN, false, false);
  controlling_thread = asciichat_thread_self();
  webrtc_peer_manager_t manager = {0};
  manager.config.on_transport_ready = ready;
  manager.config.defer_transport_ready = true;
  manager.config.user_data = &manager;
  CHECK(mutex_init(&manager.peers_mutex, "ready_test") == ASCIICHAT_OK);
  peer_entry_t *first = add_peer(&manager, 1);
  peer_entry_t *second = add_peer(&manager, 2);
  asciichat_thread_t worker;
  CHECK(asciichat_thread_create(&worker, "publish_ready", publish_ready, first) == ASCIICHAT_OK);
  CHECK(asciichat_thread_join(&worker, NULL) == ASCIICHAT_OK);
  CHECK(callbacks == 0 && atomic_ptr_load(&first->pending_transport));
  webrtc_peer_manager_dispatch_ready(&manager);
  CHECK(callbacks == 1 && released == 1 && !atomic_ptr_load(&first->pending_transport));
  CHECK(!second->is_connected && atomic_ptr_load(&second->pending_transport));
  CHECK(asciichat_thread_create(&worker, "publish_ready", publish_ready, second) == ASCIICHAT_OK);
  CHECK(asciichat_thread_join(&worker, NULL) == ASCIICHAT_OK);
  webrtc_peer_manager_dispatch_ready(&manager);
  webrtc_peer_manager_dispatch_ready(&manager);
  CHECK(callbacks == 2 && released == 2);
  remove_peer_locked(&manager, first);
  remove_peer_locked(&manager, second);
  // A session destroyed before dispatch still owns its pending transport.
  first = add_peer(&manager, 1);
  publish_ready(first);
  remove_peer_locked(&manager, first);
  CHECK(callbacks == 2 && released == 3 && !manager.peers);
  mutex_destroy(&manager.peers_mutex);
  log_destroy();
  return 0;
}
