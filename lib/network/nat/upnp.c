/**
 * @file nat/upnp.c
 * @brief Router mapping lifecycle for direct TCP listeners.
 */
#include <stdlib.h>
#include <string.h>

#include <ascii-chat/network/nat/upnp.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/time.h>

#ifdef HAVE_MINIUPNPC
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#ifdef __APPLE__
#include <natpmp.h>
#endif
#endif

#define MAPPING_LEASE_SECONDS 3600U
#define MAPPING_RETRY_NS (30ULL * NS_PER_SEC_INT)

#ifdef HAVE_MINIUPNPC
static void mapping_set_lease(nat_upnp_context_t *ctx, uint32_t seconds) {
  uint64_t now = time_get_ns();
  ctx->lease_seconds = seconds;
  ctx->expires_at_ns = now + (uint64_t)seconds * NS_PER_SEC_INT;
  ctx->refresh_at_ns = now + (uint64_t)seconds * NS_PER_SEC_INT / 2;
  ctx->is_mapped = true;
}

static asciichat_error_t upnp_map(nat_upnp_context_t *ctx) {
  char internal_port[6], external_port[6];
  safe_snprintf(internal_port, sizeof(internal_port), "%u", ctx->internal_port);
  safe_snprintf(external_port, sizeof(external_port), "%u", ctx->mapped_port);
  int result = UPNP_AddPortMapping(ctx->control_url, ctx->service_type, external_port, internal_port, ctx->internal_ip,
                                   ctx->description, "TCP", NULL, "3600");
  if (result != UPNPCOMMAND_SUCCESS) {
    return SET_ERRNO(ERROR_NETWORK, "UPnP: mapping request failed: %s", strupnperror(result));
  }
  // Some gateways silently shorten the requested lease. Read it back on every renewal.
  char client[40] = {0}, port[6] = {0}, description[80] = {0}, enabled[4] = {0}, lease[16] = {0};
  result = UPNP_GetSpecificPortMappingEntry(ctx->control_url, ctx->service_type, external_port, "TCP", NULL, client,
                                            port, description, enabled, lease);
  uint32_t seconds = 60;
  if (result == UPNPCOMMAND_SUCCESS && lease[0] >= '0' && lease[0] <= '9') {
    char *end = NULL;
    unsigned long granted = strtoul(lease, &end, 10);
    if (*end == '\0' && granted <= MAPPING_LEASE_SECONDS) {
      // A permanent mapping can still be refreshed periodically.
      seconds = granted ? (uint32_t)granted : MAPPING_LEASE_SECONDS;
    } else if (*end == '\0' && granted > MAPPING_LEASE_SECONDS) {
      seconds = MAPPING_LEASE_SECONDS;
    }
  }
  mapping_set_lease(ctx, seconds);
  return ASCIICHAT_OK;
}

static asciichat_error_t upnp_try_map_port(nat_upnp_context_t *ctx) {
  struct UPNPUrls urls = {0};
  struct IGDdatas data = {0};
  char lan_address[16] = {0};
  char external_address[16] = {0};
  struct UPNPDev *devices = upnpDiscover(2000, NULL, NULL, 0, 0, 2, NULL);
  if (!devices) {
    return SET_ERRNO(ERROR_NETWORK, "UPnP: no gateway discovered (unsupported or disabled)");
  }
#ifdef MINIUPNPC_GETVALIDIGD_7ARG
  int result = UPNP_GetValidIGD(devices, &urls, &data, lan_address, sizeof(lan_address), external_address,
                                sizeof(external_address));
#else
  int result = UPNP_GetValidIGD(devices, &urls, &data, lan_address, sizeof(lan_address));
#endif
  asciichat_error_t error = ERROR_NETWORK;
  bool connected = result == 1;
#ifdef UPNP_PRIVATEIP_IGD
  // A connected gateway behind another NAT can still map its own listener.
  connected = connected || result == UPNP_PRIVATEIP_IGD;
#endif
  if (!connected || !urls.controlURL || !lan_address[0] || strncmp(lan_address, "127.", 4) == 0 ||
      strcmp(lan_address, "0.0.0.0") == 0) {
    SET_ERRNO(ERROR_NETWORK, "UPnP: no connected gateway with a usable LAN address (IGD result %d)", result);
    goto cleanup;
  }
  result = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, external_address);
  if (result != UPNPCOMMAND_SUCCESS || !external_address[0]) {
    SET_ERRNO(ERROR_NETWORK, "UPnP: could not obtain gateway external address");
    goto cleanup;
  }
  SAFE_STRNCPY(ctx->internal_ip, lan_address, sizeof(ctx->internal_ip));
  SAFE_STRNCPY(ctx->external_ip, external_address, sizeof(ctx->external_ip));
  SAFE_STRNCPY(ctx->service_type, data.first.servicetype, sizeof(ctx->service_type));
  SAFE_STRNCPY(ctx->device_description, urls.controlURL, sizeof(ctx->device_description));
  SAFE_STRDUP(ctx->control_url, urls.controlURL);
  if (!ctx->control_url) {
    error = SET_ERRNO(ERROR_MEMORY, "UPnP: could not retain gateway control URL");
    goto cleanup;
  }
  error = upnp_map(ctx);
cleanup:
  freeUPNPDevlist(devices);
  FreeUPNPUrls(&urls);
  if (error != ASCIICHAT_OK) {
    SAFE_FREE(ctx->control_url);
    ctx->control_url = NULL;
  }
  return error;
}
#else
static asciichat_error_t upnp_try_map_port(nat_upnp_context_t *ctx) {
  (void)ctx;
  return SET_ERRNO(ERROR_NETWORK, "UPnP: support unavailable in this build");
}
#endif

#if defined(__APPLE__) && defined(HAVE_MINIUPNPC)
// libnatpmp returns TRYAGAIN while waiting and scheduling retransmissions.
static asciichat_error_t natpmp_wait(natpmp_t *pmp, natpmpresp_t *response, uint16_t type) {
  uint64_t deadline = time_get_ns() + 2ULL * NS_PER_SEC_INT;
  do {
    int result = readnatpmpresponseorretry(pmp, response);
    if (result == 0) {
      if (response->type == type && response->resultcode == 0) {
        return ASCIICHAT_OK;
      }
      return SET_ERRNO(ERROR_NETWORK, "NAT-PMP: unexpected response");
    }
    if (result != NATPMP_TRYAGAIN) {
      return SET_ERRNO(ERROR_NETWORK, "NAT-PMP: gateway request failed (%d)", result);
    }
    time_sleep_ns(10ULL * NS_PER_MS_INT);
  } while (time_get_ns() < deadline);
  return SET_ERRNO(ERROR_NETWORK, "NAT-PMP: gateway response timed out");
}

static asciichat_error_t natpmp_map(nat_upnp_context_t *ctx, uint32_t lifetime, bool discover) {
  natpmp_t pmp;
  natpmpresp_t response = {0};
  if (initnatpmp(&pmp, discover ? 0 : 1, ctx->gateway) < 0) {
    return SET_ERRNO(ERROR_NETWORK, "NAT-PMP: could not initialize gateway connection");
  }
  asciichat_error_t error = ERROR_NETWORK;
  if (discover) {
    if (sendpublicaddressrequest(&pmp) < 0) {
      SET_ERRNO(ERROR_NETWORK, "NAT-PMP: public address request failed");
      goto cleanup;
    }
    error = natpmp_wait(&pmp, &response, NATPMP_RESPTYPE_PUBLICADDRESS);
    if (error != ASCIICHAT_OK) {
      goto cleanup;
    }
    const unsigned char *ip = (const unsigned char *)&response.pnu.publicaddress.addr;
    safe_snprintf(ctx->external_ip, sizeof(ctx->external_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    ctx->gateway = pmp.gateway;
  }
  if (sendnewportmappingrequest(&pmp, NATPMP_PROTOCOL_TCP, ctx->internal_port, ctx->mapped_port, lifetime) < 0) {
    error = SET_ERRNO(ERROR_NETWORK, "NAT-PMP: mapping request failed");
    goto cleanup;
  }
  error = natpmp_wait(&pmp, &response, NATPMP_RESPTYPE_TCPPORTMAPPING);
  if (error == ASCIICHAT_OK && lifetime != 0) {
    if (!response.pnu.newportmapping.lifetime || !response.pnu.newportmapping.mappedpublicport ||
        response.pnu.newportmapping.privateport != ctx->internal_port) {
      error = SET_ERRNO(ERROR_NETWORK, "NAT-PMP: invalid granted mapping");
      goto cleanup;
    }
    ctx->mapped_port = response.pnu.newportmapping.mappedpublicport;
    ctx->is_natpmp = true;
    mapping_set_lease(ctx, response.pnu.newportmapping.lifetime);
  }
cleanup:
  closenatpmp(&pmp);
  return error;
}
#endif

asciichat_error_t nat_upnp_open(uint16_t internal_port, const char *description, nat_upnp_context_t **ctx) {
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NAT: missing mapping output");
  }
  *ctx = NULL;
  if (!internal_port || !description) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NAT: a listening port and description are required");
  }
  *ctx = SAFE_CALLOC(1, sizeof(nat_upnp_context_t), nat_upnp_context_t *);
  if (!*ctx) {
    return SET_ERRNO(ERROR_MEMORY, "NAT: could not allocate mapping context");
  }
  (*ctx)->internal_port = internal_port;
  (*ctx)->mapped_port = internal_port;
  SAFE_STRNCPY((*ctx)->description, description, sizeof((*ctx)->description));
  log_info("UPnP: discovering a gateway for TCP port %u", internal_port);
  asciichat_error_t result = upnp_try_map_port(*ctx);
#if defined(__APPLE__) && defined(HAVE_MINIUPNPC)
  if (result != ASCIICHAT_OK) {
    log_debug("UPnP unavailable; trying NAT-PMP");
    result = natpmp_map(*ctx, MAPPING_LEASE_SECONDS, true);
  }
#endif
  if (result == ASCIICHAT_OK) {
    log_info("NAT: mapping created for %s:%u (external reachability unverified)", (*ctx)->external_ip,
             (*ctx)->mapped_port);
    return ASCIICHAT_OK;
  }
  LOG_ERRNO_IF_SET("Automatic router mapping unavailable");
  SAFE_FREE((*ctx)->control_url);
  SAFE_FREE(*ctx);
  *ctx = NULL;
  return SET_ERRNO(ERROR_NETWORK, "NAT: automatic mapping unavailable; direct connectivity may still work");
}

void nat_upnp_close(nat_upnp_context_t **ctx) {
  if (!ctx || !*ctx) {
    return;
  }
  if ((*ctx)->is_mapped) {
    asciichat_error_t error = ERROR_NETWORK;
#if defined(__APPLE__) && defined(HAVE_MINIUPNPC)
    if ((*ctx)->is_natpmp) {
      error = natpmp_map(*ctx, 0, false);
    } else
#endif
    {
#ifdef HAVE_MINIUPNPC
      char port[6];
      safe_snprintf(port, sizeof(port), "%u", (*ctx)->mapped_port);
      int result = UPNP_DeletePortMapping((*ctx)->control_url, (*ctx)->service_type, port, "TCP", NULL);
      // Fios gateways require an explicit wildcard when deleting an unrestricted mapping.
      if (result == 402) {
        result = UPNP_DeletePortMapping((*ctx)->control_url, (*ctx)->service_type, port, "TCP", "*");
      }
      if (result != UPNPCOMMAND_SUCCESS && result != 714) {
        log_debug("UPnP: deletion failed: %s (%d)", strupnperror(result), result);
      }
      // An already expired mapping needs no further cleanup.
      error = (result == UPNPCOMMAND_SUCCESS || result == 714) ? ASCIICHAT_OK : ERROR_NETWORK;
#endif
    }
    if (error != ASCIICHAT_OK) {
      log_warn("NAT: could not remove TCP mapping for port %u; its lease will expire", (*ctx)->mapped_port);
    } else {
      log_info("NAT: removed TCP mapping for port %u", (*ctx)->mapped_port);
    }
  }
  SAFE_FREE((*ctx)->control_url);
  SAFE_FREE(*ctx);
  *ctx = NULL;
}

bool nat_upnp_is_active(const nat_upnp_context_t *ctx) {
  return ctx && ctx->is_mapped && ctx->external_ip[0] && time_get_ns() < ctx->expires_at_ns;
}

asciichat_error_t nat_upnp_refresh(nat_upnp_context_t *ctx) {
  if (!ctx || !ctx->is_mapped) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NAT: cannot refresh without a mapping");
  }
  asciichat_error_t result = ERROR_NETWORK;
#if defined(__APPLE__) && defined(HAVE_MINIUPNPC)
  if (ctx->is_natpmp) {
    result = natpmp_map(ctx, MAPPING_LEASE_SECONDS, false);
  } else
#endif
  {
#ifdef HAVE_MINIUPNPC
    result = upnp_map(ctx);
#endif
  }
  if (result != ASCIICHAT_OK) {
    ctx->refresh_at_ns = time_get_ns() + MAPPING_RETRY_NS;
    log_warn("NAT: mapping renewal failed; %s; retrying in 30 seconds",
             nat_upnp_is_active(ctx) ? "previous lease has not expired" : "mapping lease expired");
    return result;
  }
  log_info("NAT: mapping renewed for %s:%u", ctx->external_ip, ctx->mapped_port);
  return ASCIICHAT_OK;
}

asciichat_error_t nat_upnp_get_address(const nat_upnp_context_t *ctx, char *addr, size_t addr_len) {
  if (!ctx || !addr || addr_len < 22) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NAT: invalid arguments for get_address");
  }
  if (!nat_upnp_is_active(ctx)) {
    return SET_ERRNO(ERROR_NETWORK, "NAT: no unexpired mapping to advertise");
  }
  int written = safe_snprintf(addr, addr_len, "%s:%u", ctx->external_ip, ctx->mapped_port);
  if (written < 0 || (size_t)written >= addr_len) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NAT: address buffer too small");
  }
  return ASCIICHAT_OK;
}
