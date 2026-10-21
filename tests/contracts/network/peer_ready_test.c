#include "../worker_assert.h"
#include <criterion/criterion.h>
/* Exercise production readiness dispatch without external ICE services. */
#include "../../../lib/network/webrtc/peer_manager.c"
#include <stdlib.h>

static thread_id_t controlling_thread;
static unsigned callbacks, released;
static void destroy_transport(acip_transport_t *transport) {
  (void)transport;
  released++;
}
static const acip_transport_methods_t methods = {.destroy_impl = destroy_transport};
static void ready(acip_transport_t *transport, const uint8_t id[16], void *context) {
  cr_assert(context && thread_equal(controlling_thread, thread_self()));
  cr_assert(id[0] == 1 || id[0] == 2);
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
static void *dispatch_foreign(void *context) {
  cr_assert_worker((webrtc_peer_manager_dispatch_ready(context)) == (ERROR_INVALID_STATE));
  return NULL;
}
static asciichat_error_t send_sdp(const uint8_t session[16], const uint8_t recipient[16], const char *type,
                                  const char *sdp, void *context) {
  (void)session;
  (void)recipient;
  (void)type;
  (void)sdp;
  (void)context;
  return ASCIICHAT_OK;
}
static asciichat_error_t send_ice(const uint8_t session[16], const uint8_t recipient[16], const char *candidate,
                                  const char *mid, void *context) {
  (void)session;
  (void)recipient;
  (void)candidate;
  (void)mid;
  (void)context;
  return ASCIICHAT_OK;
}
Test(peer_ready, dispatches_on_owner_and_releases_once, .timeout = 60) {
  log_init(NULL, LOG_WARN, false, false);
  controlling_thread = thread_self();
  webrtc_peer_manager_t *manager = NULL;
  webrtc_peer_manager_config_t config = {.on_transport_ready = ready, .user_data = &manager};
  webrtc_signaling_callbacks_t signaling = {.send_sdp = send_sdp, .send_ice = send_ice};
  cr_assert_eq(webrtc_peer_manager_create(&config, &signaling, &manager), ASCIICHAT_OK);
  peer_entry_t *first = add_peer(manager, 1);
  peer_entry_t *second = add_peer(manager, 2);
  asciichat_thread_t worker;
  cr_assert_eq(acthread_create(&worker, "publish_ready", publish_ready, first), ASCIICHAT_OK);
  cr_assert_eq(thread_join(&worker, NULL), ASCIICHAT_OK);
  cr_assert_eq(acthread_create(&worker, "dispatch_foreign", dispatch_foreign, manager), ASCIICHAT_OK);
  cr_assert_eq(thread_join(&worker, NULL), ASCIICHAT_OK);
  cr_assert(callbacks == 0 && atomic_ptr_load(&first->pending_transport));
  cr_assert_eq(webrtc_peer_manager_dispatch_ready(manager), ASCIICHAT_OK);
  cr_assert(callbacks == 1 && released == 1 && !atomic_ptr_load(&first->pending_transport));
  cr_assert(!second->is_connected && atomic_ptr_load(&second->pending_transport));
  cr_assert_eq(acthread_create(&worker, "publish_ready", publish_ready, second), ASCIICHAT_OK);
  cr_assert_eq(thread_join(&worker, NULL), ASCIICHAT_OK);
  cr_assert_eq(webrtc_peer_manager_dispatch_ready(manager), ASCIICHAT_OK);
  cr_assert_eq(webrtc_peer_manager_dispatch_ready(manager), ASCIICHAT_OK);
  cr_assert(callbacks == 2 && released == 2);
  remove_peer_locked(manager, first);
  remove_peer_locked(manager, second);
  // A session destroyed before dispatch still owns its pending transport.
  first = add_peer(manager, 1);
  publish_ready(first);
  webrtc_peer_manager_destroy(manager);
  cr_assert(callbacks == 2 && released == 3);
  log_destroy();
}
