#include <ascii-chat/network/nat/upnp.h>

asciichat_error_t nat_upnp_open(uint16_t internal_port, const char *description, nat_upnp_context_t **ctx) {
  (void)internal_port;
  (void)description;
  if (ctx)
    *ctx = NULL;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "UPnP port mapping is unavailable on iOS");
}

asciichat_error_t nat_upnp_open_protocol(uint16_t internal_port, const char *description,
                                         nat_upnp_protocol_t protocol, nat_upnp_context_t **ctx) {
  (void)protocol;
  return nat_upnp_open(internal_port, description, ctx);
}

void nat_upnp_close(nat_upnp_context_t **ctx) {
  if (ctx)
    *ctx = NULL;
}

bool nat_upnp_is_active(const nat_upnp_context_t *ctx) {
  (void)ctx;
  return false;
}

bool nat_upnp_matches_bind_address(const nat_upnp_context_t *ctx, const char *bind_address) {
  (void)ctx;
  (void)bind_address;
  return false;
}

asciichat_error_t nat_upnp_get_endpoint(const nat_upnp_context_t *ctx, char *ip, size_t ip_len, uint16_t *port) {
  (void)ctx;
  (void)ip;
  (void)ip_len;
  (void)port;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "UPnP port mapping is unavailable on iOS");
}

asciichat_error_t nat_upnp_refresh(nat_upnp_context_t *ctx) {
  (void)ctx;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "UPnP port mapping is unavailable on iOS");
}

asciichat_error_t nat_upnp_get_address(const nat_upnp_context_t *ctx, char *addr, size_t addr_len) {
  (void)ctx;
  (void)addr;
  (void)addr_len;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "UPnP port mapping is unavailable on iOS");
}
