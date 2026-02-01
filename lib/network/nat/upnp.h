/**
 * @file nat/upnp.h
 * @brief UPnP/NAT-PMP port mapping for direct TCP connectivity
 * @ingroup nat
 *
 * Enables automatic port forwarding on home routers using UPnP/NAT-PMP,
 * making direct TCP connections work for ~70% of home users without WebRTC.
 *
 * **Quick Win Strategy:**
 * - Try UPnP first (works on ~90% of home routers)
 * - Fall back to NAT-PMP (Apple/Time Capsule)
 * - If both fail, client connects via ACDS discovery + WebRTC
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

/** @brief Handle to UPnP context */
typedef struct nat_upnp_context {
  char external_ip[16];         ///< Detected external/public IP (e.g., "203.0.113.42")
  uint16_t mapped_port;         ///< External port that was mapped (may differ from internal)
  uint16_t internal_port;       ///< Internal port we're binding to
  char device_description[256]; ///< Device name for logging (e.g., "TP-Link Archer C7")
  bool is_natpmp;               ///< true if NAT-PMP was used, false if UPnP
  bool is_mapped;               ///< true if port mapping is currently active
} nat_upnp_context_t;

/**
 * @brief Discover and open port via UPnP
 *
 * Attempts to find an UPnP-enabled gateway and request port mapping.
 * On success, fills in external_ip and mapped_port.
 *
 * @param internal_port Local TCP port to map (e.g., 27224 for ACDS)
 * @param description Description for port mapping (e.g., "ascii-chat Server")
 * @param[out] ctx Context handle (must be freed with nat_upnp_close())
 *
 * @return ASCIICHAT_OK if port was successfully mapped
 * @return ERROR_NETWORK_* if discovery or mapping failed (not fatal, fallback to WebRTC)
 *
 * **Example:**
 * ```c
 * nat_upnp_context_t *ctx = NULL;
 * asciichat_error_t result = nat_upnp_open(27224, "ascii-chat Server", &ctx);
 * if (result == ASCIICHAT_OK && ctx) {
 *     printf("Public IP: %s\n", ctx->external_ip);
 *     // Advertise ctx->external_ip:ctx->mapped_port to ACDS
 * }
 * ```
 */
asciichat_error_t nat_upnp_open(uint16_t internal_port, const char *description, nat_upnp_context_t **ctx);

/**
 * @brief Close port mapping and clean up
 *
 * Removes the port mapping from the gateway and frees resources.
 * Safe to call with NULL.
 *
 * @param ctx Context handle (will be set to NULL on return)
 */
void nat_upnp_close(nat_upnp_context_t **ctx);

/**
 * @brief Check if port mapping is still active
 *
 * Useful for long-running servers to verify the mapping hasn't expired.
 *
 * @param ctx Context handle
 * @return true if port is still mapped on the gateway
 */
bool nat_upnp_is_active(const nat_upnp_context_t *ctx);

/**
 * @brief Refresh port mapping (e.g., for long-running servers)
 *
 * Some gateways may expire mappings. Call periodically (e.g., every hour)
 * to ensure the mapping stays active.
 *
 * @param ctx Context handle
 * @return ASCIICHAT_OK if refresh succeeded
 */
asciichat_error_t nat_upnp_refresh(nat_upnp_context_t *ctx);

/**
 * @brief Get the public address (IP:port) for advertising to clients
 *
 * Useful for ACDS registration where we need to advertise the public
 * endpoint for P2P connections.
 *
 * @param ctx Context handle
 * @param[out] addr Buffer to write "IP:port" format (must be at least 22 bytes)
 * @param addr_len Size of addr buffer
 *
 * @return ASCIICHAT_OK if successfully written
 * @return ERROR_INVALID_PARAM if addr is too small
 */
asciichat_error_t nat_upnp_get_address(const nat_upnp_context_t *ctx, char *addr, size_t addr_len);
