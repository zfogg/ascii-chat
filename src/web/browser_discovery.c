#include <ascii-chat/network/acip/acds_protocol.h>
#include <ascii-chat/common.h>
#include <emscripten.h>

EM_JS(void, discovery_send, (acds_protocol_t * p, int type, const void *data, size_t length),
      { Module.discoverySend(p, type, HEAPU8.slice(data, data + length)); });
static asciichat_error_t browser_send(void *p, packet_type_t type, const void *payload, size_t length) {
  discovery_send(p, type, payload, length);
  return ASCIICHAT_OK;
}
EMSCRIPTEN_KEEPALIVE acds_protocol_t *browser_discovery_new(void) {
  acds_protocol_t *p = SAFE_CALLOC(1, sizeof(*p), acds_protocol_t *);
  if (p)
    acds_protocol_init(p, browser_send, p);
  return p;
}
EMSCRIPTEN_KEEPALIVE void browser_discovery_destroy(acds_protocol_t *p) {
  if (!p)
    return;
  acds_protocol_destroy(p);
  SAFE_FREE(p);
}
EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_discovery_enter(acds_protocol_t *p, bool create, const char *name,
                                                               const char *password) {
  return acds_protocol_enter(p, create, name, password);
}
EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_discovery_receive(acds_protocol_t *p, uint32_t type, const void *data,
                                                                 size_t length) {
  return acds_protocol_receive(p, (packet_type_t)type, data, length);
}
EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_discovery_leave(acds_protocol_t *p) {
  return acds_protocol_leave(p);
}
EM_JS(void, discovery_snapshot,
      (int state, bool terminal, const char *error, const char *name, const uint8_t *participant,
       const uint8_t *session, const uint8_t *host, const char *user, const char *password),
      {
        Module.discoveryResult = {
          state,
          terminal : !!terminal,
          error : UTF8ToString(error),
          name : UTF8ToString(name),
          joined : {
            participantId : HEAPU8.slice(participant, participant + 16),
            sessionId : HEAPU8.slice(session, session + 16),
            hostId : HEAPU8.slice(host, host + 16),
            turnUsername : UTF8ToString(user),
            turnPassword : UTF8ToString(password)
          }
        };
      });
EMSCRIPTEN_KEEPALIVE void browser_discovery_snapshot(acds_protocol_t *p) {
  discovery_snapshot(p->state, p->terminal, p->error, p->name, p->joined.participant_id, p->joined.session_id,
                     p->joined.host_established ? p->joined.host_id : p->joined.initiator_id, p->joined.turn_username,
                     p->joined.turn_password);
}
