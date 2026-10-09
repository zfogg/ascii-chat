#include <assert.h>
#include <ascii-chat/network/nat/upnp.h>
#include <miniupnpc/miniupnpc.h>

static uint64_t now = NS_PER_SEC_INT;
uint64_t time_get_ns(void) {
  return now;
}
void time_sleep_ns(uint64_t ns) {
  now += ns;
}
static int discover_calls, add_calls, delete_calls, add_error, delete_error;
static bool discover_ok = true, bad_lan, bad_external;
static char target[16], description[128];
static struct UPNPDev device;
static int igd_result = 1;
struct UPNPDev *upnpDiscover(int ms, const char *iface, const char *path, int port, int ipv6, unsigned char ttl,
                             int *error) {
  (void)iface;
  (void)path;
  (void)port;
  (void)ipv6;
  (void)ttl;
  (void)error;
  assert(ms == 2000);
  discover_calls++;
  return discover_ok ? &device : NULL;
}
int UPNP_GetValidIGD(struct UPNPDev *dev, struct UPNPUrls *urls, struct IGDdatas *data, char *lan, int len
#ifdef MINIUPNPC_GETVALIDIGD_7ARG
                     ,
                     char *wan, int wanlen
#endif
) {
  assert(dev == &device);
  SAFE_STRDUP(urls->controlURL, "http://192.168.1.1/control");
  snprintf(data->first.servicetype, 128, "urn:test:WANIPConnection:1");
  snprintf(lan, (size_t)len, "%s", bad_lan ? "127.0.0.1" : "192.168.1.42");
#ifdef MINIUPNPC_GETVALIDIGD_7ARG
  snprintf(wan, (size_t)wanlen, "198.51.100.20");
#endif
  return igd_result;
}
int UPNP_GetExternalIPAddress(const char *url, const char *service, char *ip) {
  (void)url;
  (void)service;
  snprintf(ip, 16, "198.51.100.20");
  return bad_external ? 501 : 0;
}
int UPNP_AddPortMapping(const char *url, const char *service, const char *external, const char *internal,
                        const char *client, const char *desc, const char *proto, const char *remote,
                        const char *lease) {
  assert(strcmp(url, "http://192.168.1.1/control") == 0);
  assert(strcmp(service, "urn:test:WANIPConnection:1") == 0);
  assert(strcmp(external, "27224") == 0 && strcmp(internal, "27224") == 0);
  assert(strcmp(proto, "TCP") == 0 && remote == NULL && strcmp(lease, "3600") == 0);
  snprintf(target, sizeof(target), "%s", client);
  snprintf(description, sizeof(description), "%s", desc);
  add_calls++;
  return add_error;
}
int UPNP_DeletePortMapping(const char *url, const char *service, const char *port, const char *proto,
                           const char *remote) {
  assert(strcmp(url, "http://192.168.1.1/control") == 0);
  assert(strcmp(service, "urn:test:WANIPConnection:1") == 0);
  assert(strcmp(port, "27224") == 0 && strcmp(proto, "TCP") == 0 && !remote);
  delete_calls++;
  return delete_error;
}
void freeUPNPDevlist(struct UPNPDev *dev) {
  assert(dev == &device);
}
void FreeUPNPUrls(struct UPNPUrls *urls) {
  free(urls->controlURL);
  urls->controlURL = NULL;
}
const char *strupnperror(int error) {
  (void)error;
  return "fake refusal";
}

static bool pmp_enabled, pmp_timeout;
static int pmp_type, pmp_reads, pmp_error, pmp_open, pmp_close, pmp_deletes;
static uint32_t pmp_lifetime;
int initnatpmp(natpmp_t *pmp, int forced, uint32_t gateway) {
  if (!pmp_enabled)
    return -1;
  if (forced)
    assert(gateway == 1234);
  pmp->gateway = 1234;
  pmp_open++;
  return 0;
}
int closenatpmp(natpmp_t *pmp) {
  (void)pmp;
  pmp_close++;
  return 0;
}
int sendpublicaddressrequest(natpmp_t *pmp) {
  (void)pmp;
  pmp_type = 0;
  pmp_reads = 0;
  return 2;
}
int sendnewportmappingrequest(natpmp_t *pmp, int proto, uint16_t internal, uint16_t external, uint32_t lifetime) {
  (void)pmp;
  assert(proto == NATPMP_PROTOCOL_TCP && internal == 27224);
  assert(external == 27224 || external == 30000);
  pmp_type = 2;
  pmp_reads = 0;
  pmp_lifetime = lifetime;
  if (!lifetime)
    pmp_deletes++;
  return 12;
}
int readnatpmpresponseorretry(natpmp_t *pmp, natpmpresp_t *resp) {
  (void)pmp;
  if (pmp_error)
    return pmp_error;
  if (pmp_timeout || pmp_reads++ == 0)
    return NATPMP_TRYAGAIN;
  resp->type = (uint16_t)pmp_type;
  resp->resultcode = 0;
  if (!pmp_type) {
    unsigned char ip[] = {198, 51, 100, 21};
    memcpy(&resp->pnu.publicaddress.addr, ip, sizeof(ip));
  } else {
    resp->pnu.newportmapping.privateport = 27224;
    resp->pnu.newportmapping.mappedpublicport = 30000;
    resp->pnu.newportmapping.lifetime = pmp_lifetime ? 120 : 0;
  }
  return 0;
}

int main(void) {
  (void)pmp_open;
  (void)pmp_close;
  (void)pmp_deletes;
  (void)add_calls;
  (void)delete_calls;
  nat_upnp_context_t *ctx = NULL;
  assert(nat_upnp_open(0, "test", &ctx) == ERROR_INVALID_PARAM && !ctx);
  assert(discover_calls == 0);
  assert(nat_upnp_open(27224, NULL, &ctx) == ERROR_INVALID_PARAM);
  assert(nat_upnp_open(27224, "test", NULL) == ERROR_INVALID_PARAM);
  assert(nat_upnp_refresh(NULL) == ERROR_INVALID_PARAM);
  nat_upnp_close(NULL);
  nat_upnp_close(&ctx);
#ifndef HAVE_MINIUPNPC
  assert(nat_upnp_open(27224, "test", &ctx) == ERROR_NETWORK && !ctx);
#else
  assert(nat_upnp_open(27224, "test listener", &ctx) == ASCIICHAT_OK);
  assert(strcmp(target, "192.168.1.42") == 0);
  assert(strcmp(description, "test listener") == 0);
  assert(nat_upnp_is_active(ctx));
  assert(ctx->refresh_at_ns == now + 1800ULL * NS_PER_SEC_INT);
  char endpoint[22];
  assert(nat_upnp_get_address(ctx, endpoint, sizeof(endpoint)) == ASCIICHAT_OK);
  assert(strcmp(endpoint, "198.51.100.20:27224") == 0);
  now = ctx->refresh_at_ns;
  assert(nat_upnp_refresh(ctx) == ASCIICHAT_OK && add_calls == 2);
  assert(ctx->expires_at_ns == now + 3600ULL * NS_PER_SEC_INT);
  // Rejection retains the old expiry, schedules a retry, and stops advertising after expiry.
  add_error = 718;
  uint64_t expiry = ctx->expires_at_ns;
  assert(nat_upnp_refresh(ctx) == ERROR_NETWORK);
  assert(ctx->expires_at_ns == expiry && ctx->refresh_at_ns == now + 30ULL * NS_PER_SEC_INT);
  now = expiry;
  assert(!nat_upnp_is_active(ctx));
  assert(nat_upnp_get_address(ctx, endpoint, sizeof(endpoint)) == ERROR_NETWORK);
  add_error = 0;
  assert(nat_upnp_refresh(ctx) == ASCIICHAT_OK && nat_upnp_is_active(ctx));
  nat_upnp_close(&ctx);
  assert(ctx == NULL && delete_calls == 1);
  nat_upnp_close(&ctx);
  assert(delete_calls == 1);
#ifdef UPNP_PRIVATEIP_IGD
  igd_result = UPNP_PRIVATEIP_IGD;
  assert(nat_upnp_open(27224, "double NAT", &ctx) == ASCIICHAT_OK);
  nat_upnp_close(&ctx);
#endif
  igd_result = 3;
  assert(nat_upnp_open(27224, "disconnected", &ctx) == ERROR_NETWORK && !ctx);
  igd_result = 1;
  // Mapping refusal, missing gateway, invalid LAN address, and external-IP failure.
  add_error = 718;
  assert(nat_upnp_open(27224, "test", &ctx) == ERROR_NETWORK && !ctx);
  add_error = 0;
  discover_ok = false;
  assert(nat_upnp_open(27224, "test", &ctx) == ERROR_NETWORK && !ctx);
  discover_ok = true;
  bad_lan = true;
  assert(nat_upnp_open(27224, "test", &ctx) == ERROR_NETWORK && !ctx);
  bad_lan = false;
  bad_external = true;
  assert(nat_upnp_open(27224, "test", &ctx) == ERROR_NETWORK && !ctx);
  bad_external = false;
  assert(nat_upnp_open(27224, "test", &ctx) == ASCIICHAT_OK);
  delete_error = 501;
  nat_upnp_close(&ctx);
  assert(ctx == NULL);
#ifdef __APPLE__
  discover_ok = false;
  pmp_enabled = true;
  assert(nat_upnp_open(27224, "test", &ctx) == ASCIICHAT_OK);
  assert(ctx->is_natpmp && ctx->mapped_port == 30000 && ctx->lease_seconds == 120);
  assert(ctx->refresh_at_ns == now + 60ULL * NS_PER_SEC_INT);
  assert(strcmp(ctx->external_ip, "198.51.100.21") == 0);
  now = ctx->refresh_at_ns;
  assert(nat_upnp_refresh(ctx) == ASCIICHAT_OK);
  nat_upnp_close(&ctx);
  assert(!ctx && pmp_deletes == 1 && pmp_open == pmp_close);
  pmp_timeout = true;
  uint64_t start = now;
  assert(nat_upnp_open(27224, "test", &ctx) == ERROR_NETWORK && !ctx);
  assert(now - start == 2ULL * NS_PER_SEC_INT);
  pmp_timeout = false;
  pmp_error = -51;
  assert(nat_upnp_open(27224, "test", &ctx) == ERROR_NETWORK && !ctx);
  assert(pmp_open == pmp_close);
#endif
#endif
  return 0;
}
