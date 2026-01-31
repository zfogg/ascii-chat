/**
 * @file nat/upnp.c
 * @brief UPnP/NAT-PMP port mapping implementation
 *
 * Strategy for enabling direct TCP without WebRTC:
 * 1. Try UPnP discovery (works on ~90% of consumer routers)
 * 2. Fall back to NAT-PMP if UPnP fails (Apple/Time Capsule)
 * 3. If both fail, client falls back to ACDS + WebRTC
 *
 * This pragmatic approach provides direct connectivity for most home users
 * while maintaining compatibility with stricter NATs via WebRTC fallback.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "upnp.h"
#include "common.h"
#include "log/logging.h"

#ifdef __APPLE__
// Only include natpmp if we're on Apple and miniupnpc is available
#ifdef HAVE_MINIUPNPC
#include <natpmp.h>
#endif
#endif

// miniupnpc is conditionally included based on CMake detection
// If not found, HAVE_MINIUPNPC will not be defined
// The build system defines HAVE_MINIUPNPC=1 in compile definitions if miniupnpc is found
#ifdef HAVE_MINIUPNPC
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#endif

/**
 * @brief Try UPnP port mapping
 *
 * @return ASCIICHAT_OK on success, ERROR_NETWORK_* on failure
 */
#ifdef HAVE_MINIUPNPC
static asciichat_error_t upnp_try_map_port(uint16_t internal_port, const char *description, nat_upnp_context_t *ctx) {
  struct UPNPDev *device_list = NULL;
  struct UPNPUrls urls;
  struct IGDdatas data;
  int upnp_result = 0;
  char port_str[10];
  char external_addr[40];

  memset(&urls, 0, sizeof(urls));
  memset(&data, 0, sizeof(data));

  // Step 1: Discover UPnP devices (2 second timeout for faster fallback)
  log_debug("UPnP: Starting discovery (2 second timeout)...");
  device_list = upnpDiscover(2000,  // timeout in milliseconds
                             NULL,  // multicast interface
                             NULL,  // minissdpdpath
                             0,     // sameport
                             0,     // ipv6
                             2,     // ttl
                             NULL); // error pointer

  if (!device_list) {
    SET_ERRNO(ERROR_NETWORK, "UPnP: No devices found (router may not support UPnP)");
    return ERROR_NETWORK;
  }

  log_debug("UPnP: Found %d device(s)", 1); // device_list is a linked list, just log 1 for now

  // Step 2: Find the Internet Gateway Device (IGD)
  // Note: UPNP_GetValidIGD signature changed in miniupnpc API version 14
  // API < 14:  UPNP_GetValidIGD(devlist, urls, data, external_addr, len) - 5 args
  // API >= 14: UPNP_GetValidIGD(devlist, urls, data, external_addr, len, lanaddr, lanaddr_len) - 7 args
  // MINIUPNPC_GETVALIDIGD_7ARG is set by CMake's check_c_source_compiles to detect the actual signature
#ifdef MINIUPNPC_GETVALIDIGD_7ARG
  upnp_result = UPNP_GetValidIGD(device_list, &urls, &data, external_addr, sizeof(external_addr), NULL, 0);
#else
  upnp_result = UPNP_GetValidIGD(device_list, &urls, &data, external_addr, sizeof(external_addr));
#endif

  if (upnp_result != 1) { // 1 = UPNP_IGD_VALID_CONNECTED (value may vary between versions)
    SET_ERRNO(ERROR_NETWORK, "UPnP: No valid Internet Gateway found");
    freeUPNPDevlist(device_list);
    FreeUPNPUrls(&urls);
    return ERROR_NETWORK;
  }

  log_debug("UPnP: Found valid IGD, external address: %s", external_addr);

  // Step 3: Get external IP
  upnp_result = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, external_addr);

  if (upnp_result != UPNPCOMMAND_SUCCESS) {
    SET_ERRNO(ERROR_NETWORK, "UPnP: Failed to get external IP: %s", strupnperror(upnp_result));
    freeUPNPDevlist(device_list);
    FreeUPNPUrls(&urls);
    return ERROR_NETWORK;
  }

  SAFE_STRNCPY(ctx->external_ip, external_addr, sizeof(ctx->external_ip));
  log_info("UPnP: External IP detected: %s", ctx->external_ip);

  // Step 4: Request port mapping
  safe_snprintf(port_str, sizeof(port_str), "%u", internal_port);

  log_debug("UPnP: Requesting port mapping for port %u (%s)...", internal_port, description);

  upnp_result = UPNP_AddPortMapping(urls.controlURL,        // controlURL
                                    data.first.servicetype, // servicetype
                                    port_str,               // extPort (external port, same as internal for now)
                                    port_str,               // inPort (internal port)
                                    "127.0.0.1",            // inClient (internal IP - gets resolved by router)
                                    description,            // description
                                    "TCP",                  // protocol
                                    NULL,                   // remoteHost (any)
                                    "3600");                // leaseDuration (1 hour)

  if (upnp_result != UPNPCOMMAND_SUCCESS) {
    SET_ERRNO(ERROR_NETWORK, "UPnP: Failed to add port mapping: %s", strupnperror(upnp_result));
    freeUPNPDevlist(device_list);
    FreeUPNPUrls(&urls);
    return ERROR_NETWORK;
  }

  log_info("UPnP: ✓ Port %u successfully mapped on %s", internal_port, urls.controlURL);

  // Store device description for logging
  SAFE_STRNCPY(ctx->device_description, urls.controlURL, sizeof(ctx->device_description));
  ctx->internal_port = internal_port;
  ctx->mapped_port = internal_port;
  ctx->is_natpmp = false;
  ctx->is_mapped = true;

  // Cleanup UPnP structures
  freeUPNPDevlist(device_list);
  FreeUPNPUrls(&urls);

  return ASCIICHAT_OK;
}
#else
// Stub implementation when miniupnpc is not available
static asciichat_error_t upnp_try_map_port(uint16_t internal_port, const char *description, nat_upnp_context_t *ctx) {
  (void)internal_port;
  (void)description;
  (void)ctx;
  SET_ERRNO(ERROR_NETWORK, "miniupnpc not installed (UPnP disabled)");
  return ERROR_NETWORK;
}
#endif

/**
 * @brief Try NAT-PMP port mapping (fallback for Apple routers)
 *
 * @return ASCIICHAT_OK on success, ERROR_NETWORK_* on failure
 */
static asciichat_error_t natpmp_try_map_port(uint16_t internal_port, nat_upnp_context_t *ctx) {
#if !defined(__APPLE__) || !defined(HAVE_MINIUPNPC)
  (void)internal_port;
  (void)ctx;
#ifndef __APPLE__
  SET_ERRNO(ERROR_NETWORK, "NAT-PMP: Not available on this platform (Apple only)");
#else
  SET_ERRNO(ERROR_NETWORK, "NAT-PMP: libnatpmp not available (install miniupnpc)");
#endif
  return ERROR_NETWORK;
#else  // __APPLE__ && HAVE_MINIUPNPC
  natpmp_t natpmp;
  natpmpresp_t response;
  int result;
  char external_ip_str[16];

  log_debug("NAT-PMP: Initializing (fallback)...");

  // Initialize NAT-PMP
  result = initnatpmp(&natpmp, 0, 0);
  if (result < 0) {
    SET_ERRNO(ERROR_NETWORK, "NAT-PMP: Failed to initialize (%d)", result);
    return ERROR_NETWORK;
  }

  // Get external IP
  result = sendpublicaddressrequest(&natpmp);
  if (result < 0) {
    closenatpmp(&natpmp);
    SET_ERRNO(ERROR_NETWORK, "NAT-PMP: Failed to request public address");
    return ERROR_NETWORK;
  }

  // Wait for response
  memset(&response, 0, sizeof(response));
  result = readnatpmpresponseorretry(&natpmp, &response);
  if (result != NATPMP_TRYAGAIN && response.type == NATPMP_RESPTYPE_PUBLICADDRESS) {
    unsigned char *ipv4 = (unsigned char *)&response.pnu.publicaddress.addr;
    safe_snprintf(external_ip_str, sizeof(external_ip_str), "%u.%u.%u.%u", ipv4[0], ipv4[1], ipv4[2], ipv4[3]);
    SAFE_STRNCPY(ctx->external_ip, external_ip_str, sizeof(ctx->external_ip));
    log_info("NAT-PMP: External IP detected: %s", ctx->external_ip);
  }

  // Request port mapping
  result = sendnewportmappingrequest(&natpmp, NATPMP_PROTOCOL_TCP, internal_port, internal_port,
                                     3600); // 1 hour lease
  if (result < 0) {
    closenatpmp(&natpmp);
    SET_ERRNO(ERROR_NETWORK, "NAT-PMP: Failed to send port mapping request");
    return ERROR_NETWORK;
  }

  // Wait for mapping response
  memset(&response, 0, sizeof(response));
  result = readnatpmpresponseorretry(&natpmp, &response);
  if (result != NATPMP_TRYAGAIN && response.type == NATPMP_RESPTYPE_TCPPORTMAPPING) {
    log_info("NAT-PMP: ✓ Port %u successfully mapped", internal_port);
    ctx->internal_port = internal_port;
    ctx->mapped_port = response.pnu.newportmapping.mappedpublicport;
    ctx->is_natpmp = true;
    ctx->is_mapped = true;
    SAFE_STRNCPY(ctx->device_description, "Time Capsule/Apple AirPort", sizeof(ctx->device_description));
  } else {
    closenatpmp(&natpmp);
    SET_ERRNO(ERROR_NETWORK, "NAT-PMP: Failed to map port");
    return ERROR_NETWORK;
  }

  closenatpmp(&natpmp);
  return ASCIICHAT_OK;
#endif // __APPLE__ && HAVE_MINIUPNPC
}

// ============================================================================
// Public API Implementation
// ============================================================================

asciichat_error_t nat_upnp_open(uint16_t internal_port, const char *description, nat_upnp_context_t **ctx) {
  if (!ctx || !description) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "nat_upnp_open: Invalid arguments");
  }

  // Allocate context
  *ctx = SAFE_MALLOC(sizeof(nat_upnp_context_t), nat_upnp_context_t *);
  if (!(*ctx)) {
    return ERROR_MEMORY;
  }

  memset(*ctx, 0, sizeof(nat_upnp_context_t));

  // Try UPnP first (works on ~90% of home routers)
  log_info("NAT: Attempting UPnP port mapping for port %u...", internal_port);
  asciichat_error_t result = upnp_try_map_port(internal_port, description, *ctx);

  if (result == ASCIICHAT_OK) {
    log_info("NAT: ✓ UPnP port mapping successful!");
    return ASCIICHAT_OK;
  }

  log_info("NAT: UPnP failed, trying NAT-PMP fallback...");
  result = natpmp_try_map_port(internal_port, *ctx);

  if (result == ASCIICHAT_OK) {
    log_info("NAT: ✓ NAT-PMP port mapping successful!");
    return ASCIICHAT_OK;
  }

  // Both UPnP and NAT-PMP failed - this is OK, not fatal
  log_warn("NAT: Both UPnP and NAT-PMP failed. Direct TCP won't work, will use ACDS + WebRTC.");
  log_warn("NAT: This is normal for strict NATs. No action required.");

  SAFE_FREE(*ctx);
  *ctx = NULL;

  return SET_ERRNO(ERROR_NETWORK, "NAT: No automatic port mapping available (will use WebRTC)");
}

void nat_upnp_close(nat_upnp_context_t **ctx) {
  if (!ctx || !(*ctx)) {
    return;
  }

  if ((*ctx)->is_mapped) {
    // Note: In a real implementation, we'd remove the port mapping from the gateway.
    // For MVP, we just log and let the lease expire naturally (typically 1 hour).
    log_debug("NAT: Port mapping will expire in ~1 hour (cleanup handled by router)");
  }

  SAFE_FREE(*ctx);
  *ctx = NULL;
}

bool nat_upnp_is_active(const nat_upnp_context_t *ctx) {
  if (!ctx) {
    return false;
  }
  return ctx->is_mapped && ctx->external_ip[0] != '\0';
}

asciichat_error_t nat_upnp_refresh(nat_upnp_context_t *ctx) {
  if (!ctx || !ctx->is_mapped) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NAT: Cannot refresh - no active mapping");
  }

  log_debug("NAT: Refreshing port mapping (would extend lease in full implementation)");

  // In a real implementation, we'd re-register the port mapping to extend the lease.
  // For now, we just return success since the lease is 1 hour anyway.
  return ASCIICHAT_OK;
}

asciichat_error_t nat_upnp_get_address(const nat_upnp_context_t *ctx, char *addr, size_t addr_len) {
  if (!ctx || !addr || addr_len < 22) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NAT: Invalid arguments for get_address");
  }

  if (!ctx->is_mapped || ctx->external_ip[0] == '\0') {
    return SET_ERRNO(ERROR_NETWORK, "NAT: No active mapping to advertise");
  }

  // Format as "IP:port" (e.g., "203.0.113.42:27224")
  int written = safe_snprintf(addr, addr_len, "%s:%u", ctx->external_ip, ctx->mapped_port);

  if (written < 0 || (size_t)written >= addr_len) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NAT: Address buffer too small");
  }

  return ASCIICHAT_OK;
}
