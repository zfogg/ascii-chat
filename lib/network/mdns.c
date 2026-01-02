/**
 * @file lib/network/mdns.c
 * @brief mDNS service discovery implementation for ASCII-Chat
 *
 * Wraps the mdns library (https://github.com/mjansson/mdns) with ASCII-Chat specific API.
 * This implementation provides service advertisement and discovery for LAN-based sessions.
 */

#include "network/mdns.h"
#include "common.h"
#include "platform/socket.h"
#include <string.h>
#include <stdio.h>
#include <mdns.h>

/** @brief Internal mDNS context structure */
struct asciichat_mdns_t {
  int socket_fd;                                 /**< UDP socket for mDNS */
  asciichat_mdns_discovery_callback_fn callback; /**< Discovery callback */
  void *callback_data;                           /**< User data for callback */
};

asciichat_mdns_t *asciichat_mdns_init(void) {
  asciichat_mdns_t *mdns = SAFE_MALLOC(sizeof(asciichat_mdns_t), asciichat_mdns_t *);
  if (!mdns) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate mDNS context");
    return NULL;
  }

  memset(mdns, 0, sizeof(asciichat_mdns_t));

  /* Open IPv4 mDNS socket */
  mdns->socket_fd = mdns_socket_open_ipv4(NULL);
  if (mdns->socket_fd < 0) {
    SAFE_FREE(mdns);
    SET_ERRNO(ERROR_NETWORK_BIND, "Failed to open mDNS socket");
    return NULL;
  }

  log_info("mDNS context initialized (socket: %d)", mdns->socket_fd);
  return mdns;
}

void asciichat_mdns_shutdown(asciichat_mdns_t *mdns) {
  if (!mdns) {
    return;
  }

  if (mdns->socket_fd >= 0) {
    mdns_socket_close(mdns->socket_fd);
  }

  SAFE_FREE(mdns);
  log_info("mDNS context shutdown");
}

asciichat_error_t asciichat_mdns_advertise(asciichat_mdns_t *mdns, const asciichat_mdns_service_t *service) {
  if (!mdns || !service) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mDNS context or service is NULL");
  }

  if (!service->name || !service->type || !service->host) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Service name, type, or host is NULL");
  }

  log_info("Advertising mDNS service: %s (%s:%d)", service->name, service->host, service->port);

  /* TODO: Implement actual advertisement using mdns library
   * This will involve creating service records and sending announcements
   * The mdns library provides mdns_announce_* functions for this
   */
  return ASCIICHAT_OK;
}

asciichat_error_t asciichat_mdns_unadvertise(asciichat_mdns_t *mdns, const char *service_name) {
  if (!mdns || !service_name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mDNS context or service name is NULL");
  }

  log_info("Stopped advertising service: %s", service_name);

  /* TODO: Implement actual unadvertisement
   * This will involve sending goodbye records with TTL=0
   */
  return ASCIICHAT_OK;
}

/**
 * @brief Internal callback for mdns library to process records
 */
static int mdns_record_callback(int sock, const struct sockaddr *from, size_t addrlen, mdns_entry_type_t entry,
                                uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void *data,
                                size_t size, size_t name_offset, size_t name_length, size_t record_offset,
                                size_t record_length, void *user_data) {
  asciichat_mdns_t *mdns = (asciichat_mdns_t *)user_data;

  if (!mdns || !mdns->callback) {
    return 0;
  }

  /* Only process answers */
  if (entry != MDNS_ENTRYTYPE_ANSWER) {
    return 0;
  }

  asciichat_mdns_discovery_t discovery;
  memset(&discovery, 0, sizeof(discovery));
  discovery.ttl = ttl;

  /* TODO: Extract and format service information from mdns data
   * Use mdns_string_extract() and mdns_*_parse() functions to decode records
   * Populate the discovery structure with extracted data
   */

  /* Call the user callback with discovered service */
  mdns->callback(&discovery, mdns->callback_data);

  (void)sock;
  (void)query_id;
  (void)rclass;
  (void)rtype;
  (void)record_offset;
  (void)record_length;
  (void)addrlen;
  (void)from;
  (void)data;
  (void)size;
  (void)name_offset;
  (void)name_length;
  return 0;
}

asciichat_error_t asciichat_mdns_query(asciichat_mdns_t *mdns, const char *service_type,
                                       asciichat_mdns_discovery_callback_fn callback, void *user_data) {
  if (!mdns || !service_type || !callback) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mDNS query parameters");
  }

  mdns->callback = callback;
  mdns->callback_data = user_data;

  log_info("Starting mDNS query for: %s", service_type);

  /* TODO: Implement actual query sending using mdns library
   * Use mdns_query_send() to send PTR queries for service discovery
   */
  return ASCIICHAT_OK;
}

asciichat_error_t asciichat_mdns_update(asciichat_mdns_t *mdns, int timeout_ms) {
  if (!mdns) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mDNS context is NULL");
  }

  /* TODO: Implement actual mDNS packet processing
   * Use mdns_socket_listen() and mdns_query_recv() to handle incoming packets
   * Process queries and announcements from the network
   */

  (void)timeout_ms;
  return ASCIICHAT_OK;
}

int asciichat_mdns_get_socket(asciichat_mdns_t *mdns) {
  if (!mdns) {
    return -1;
  }
  return mdns->socket_fd;
}
