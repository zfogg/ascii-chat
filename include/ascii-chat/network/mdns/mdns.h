#pragma once

/**
 * @file lib/network/mdns.h
 * @brief mDNS (Multicast DNS) service discovery for ascii-chat
 * @ingroup network
 *
 * Provides cross-platform service advertisement and discovery for LAN.
 *
 * ## Usage
 *
 * ### Server-side (advertise service)
 * ```c
 * asciichat_mdns_service_t service = {
 *     .name = "my-session",
 *     .port = 9999,
 *     .type = "_ascii-chat._tcp",
 *     .host = "mycomputer.local"
 * };
 *
 * asciichat_mdns_t *mdns = asciichat_mdns_init();
 * asciichat_mdns_advertise(mdns, &service);
 *
 * // Keep the mdns object alive while advertising
 * // Call asciichat_mdns_update() periodically from main loop
 * while (server_running) {
 *     asciichat_mdns_update(mdns);
 *     // ... handle other events ...
 * }
 *
 * asciichat_mdns_shutdown(mdns);
 * ```
 *
 * ### Client-side (discover services)
 * ```c
 * asciichat_mdns_t *mdns = asciichat_mdns_init();
 * asciichat_mdns_query(mdns, "_ascii-chat._tcp.local");
 *
 * // Results arrive via callback
 * asciichat_mdns_update(mdns);
 *
 * asciichat_mdns_shutdown(mdns);
 * ```
 *
 * @author Claude <claude@anthropic.com>
 * @date January 2026
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ascii-chat/common.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief opaque mDNS context handle */
typedef struct asciichat_mdns_t asciichat_mdns_t;

/**
 * @brief Service information for advertisement
 */
typedef struct {
  /** Service instance name (e.g., "swift-river-canyon") */
  const char *name;
  /** Port number (e.g., 9999) */
  uint16_t port;
  /** Service type (e.g., "_ascii-chat._tcp") */
  const char *type;
  /** Host name with .local suffix (e.g., "myhost.local") */
  const char *host;
  /** Optional TXT record properties (null-terminated array of "key=value" strings) */
  const char **txt_records;
  /** Number of TXT records */
  size_t txt_count;
} asciichat_mdns_service_t;

/**
 * @brief Discovered service information
 */
typedef struct {
  /** Service instance name */
  char name[256];
  /** Service type */
  char type[256];
  /** Host name */
  char host[256];
  /** Port number */
  uint16_t port;
  /** IPv4 address (dotted decimal, if available) */
  char ipv4[16];
  /** IPv6 address (if available) */
  char ipv6[46];
  /** TXT record data */
  char txt[512];
  /** TTL remaining (seconds) */
  uint32_t ttl;
} asciichat_mdns_discovery_t;

/**
 * @brief Callback for discovered services
 *
 * @param service Discovered service information
 * @param user_data User pointer passed to query
 */
typedef void (*asciichat_mdns_discovery_callback_fn)(const asciichat_mdns_discovery_t *service, void *user_data);

/**
 * @brief Initialize mDNS context
 *
 * @return Opaque mDNS context, or NULL on error
 */
asciichat_mdns_t *asciichat_mdns_init(void);

/**
 * @brief Shutdown mDNS context and cleanup
 *
 * @param mdns Context to cleanup
 */
void asciichat_mdns_shutdown(asciichat_mdns_t *mdns);

/**
 * @brief Advertise a service on the local network
 *
 * @param mdns mDNS context
 * @param service Service to advertise
 * @return ASCIICHAT_OK on success, error code otherwise
 *
 * @note The service structure should remain valid until unadvertised
 */
asciichat_error_t asciichat_mdns_advertise(asciichat_mdns_t *mdns, const asciichat_mdns_service_t *service);

/**
 * @brief Stop advertising a service
 *
 * @param mdns mDNS context
 * @param service_name Service instance name to unadvertise
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t asciichat_mdns_unadvertise(asciichat_mdns_t *mdns, const char *service_name);

/**
 * @brief Query for services on the local network
 *
 * @param mdns mDNS context
 * @param service_type Service type to query (e.g., "_ascii-chat._tcp.local")
 * @param callback Function to call for each discovered service
 * @param user_data User pointer passed to callback
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t asciichat_mdns_query(asciichat_mdns_t *mdns, const char *service_type,
                                       asciichat_mdns_discovery_callback_fn callback, void *user_data);

/**
 * @brief Process pending mDNS events (must be called regularly)
 *
 * This should be called from the main event loop to:
 * - Send advertisement packets
 * - Receive and parse query responses
 * - Invoke discovery callbacks
 *
 * @param mdns mDNS context
 * @param timeout_ms Maximum time to block (0 = non-blocking)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t asciichat_mdns_update(asciichat_mdns_t *mdns, int timeout_ms);

/**
 * @brief Get the socket file descriptor for integration with select/poll
 *
 * @param mdns mDNS context
 * @return Socket descriptor, or -1 on error
 *
 * @note Useful for integrating mDNS into existing event loops
 */
int asciichat_mdns_get_socket(asciichat_mdns_t *mdns);

#ifdef __cplusplus
}
#endif
