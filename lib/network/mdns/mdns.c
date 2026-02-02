/**
 * @file lib/network/mdns/mdns.c
 * @brief mDNS service discovery implementation for ascii-chat
 *
 * Wraps the mdns library (https://github.com/mjansson/mdns) with ascii-chat specific API.
 * This implementation provides service advertisement and discovery for LAN-based sessions.
 */

#include <ascii-chat/network/mdns/mdns.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/socket.h>
#include <string.h>
#include <stdio.h>
#include <ascii-chat-deps/mdns/mdns.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h> /* For struct sockaddr_in, sockaddr_in6 */
#endif

/** @brief Internal mDNS context structure */
struct asciichat_mdns_t {
  int socket_fd;                                 /**< UDP socket for mDNS */
  asciichat_mdns_discovery_callback_fn callback; /**< Discovery callback */
  void *callback_data;                           /**< User data for callback */
  uint8_t *buffer;                               /**< I/O buffer for mDNS packets */
  size_t buffer_capacity;                        /**< Buffer capacity */
  uint16_t query_id;                             /**< Current query ID */
};

/** @brief mDNS packet buffer size (4KB should handle most service records) */
#define MDNS_BUFFER_SIZE (4 * 1024)

asciichat_mdns_t *asciichat_mdns_init(void) {
  asciichat_mdns_t *mdns = SAFE_MALLOC(sizeof(asciichat_mdns_t), asciichat_mdns_t *);
  if (!mdns) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate mDNS context");
    return NULL;
  }

  memset(mdns, 0, sizeof(asciichat_mdns_t));

  /* Allocate I/O buffer for mDNS packets */
  mdns->buffer = SAFE_MALLOC(MDNS_BUFFER_SIZE, uint8_t *);
  if (!mdns->buffer) {
    SAFE_FREE(mdns);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate mDNS buffer");
    return NULL;
  }
  mdns->buffer_capacity = MDNS_BUFFER_SIZE;

  /* Open IPv4 mDNS socket */
  mdns->socket_fd = mdns_socket_open_ipv4(NULL);
  if (mdns->socket_fd < 0) {
    SAFE_FREE(mdns->buffer);
    SAFE_FREE(mdns);
    SET_ERRNO(ERROR_NETWORK_BIND, "Failed to open mDNS socket");
    return NULL;
  }

  log_debug("mDNS context initialized (socket: %d, buffer: %zu bytes)", mdns->socket_fd, mdns->buffer_capacity);
  return mdns;
}

void asciichat_mdns_shutdown(asciichat_mdns_t *mdns) {
  if (!mdns) {
    return;
  }

  if (mdns->socket_fd >= 0) {
    mdns_socket_close(mdns->socket_fd);
  }

  SAFE_FREE(mdns->buffer);
  SAFE_FREE(mdns);
  log_debug("mDNS context shutdown");
}

asciichat_error_t asciichat_mdns_advertise(asciichat_mdns_t *mdns, const asciichat_mdns_service_t *service) {
  if (!mdns || !service) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mDNS context or service is NULL");
  }

  if (!service->name || !service->type || !service->host) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Service name, type, or host is NULL");
  }

  log_debug("Advertising mDNS service: %s (%s:%d)", service->name, service->host, service->port);

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
 *
 * Called by mdns_query_recv for each record received.
 * Extracts service name, type, addresses, and port information.
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

  /* Extract service type and name from DNS name format
   * For PTR records, data contains the target name (service instance name)
   */
  size_t offset = name_offset;
  mdns_string_extract(data, size, &offset, discovery.type, sizeof(discovery.type));

  /* Parse record based on type */
  switch (rtype) {
  case MDNS_RECORDTYPE_PTR: {
    /* PTR record: points to a service instance (e.g., "MyChat" for "_ascii-chat._tcp.local")
     * The record data contains the target name (the service instance name)
     */
    offset = record_offset;
    mdns_string_extract(data, size, &offset, discovery.name, sizeof(discovery.name));
    log_debug("mDNS PTR: %s -> %s (TTL: %u)", discovery.type, discovery.name, ttl);
    break;
  }

  case MDNS_RECORDTYPE_SRV: {
    /* SRV record: service details (port, target host)
     * mdns_record_parse_srv extracts the target hostname into a buffer
     * and also contains priority, weight, port in the returned structure
     */
    mdns_record_srv_t srv =
        mdns_record_parse_srv(data, size, record_offset, record_length, discovery.host, sizeof(discovery.host));
    discovery.port = srv.port;
    log_debug("mDNS SRV: %s -> %s:%u (TTL: %u)", discovery.type, discovery.host, srv.port, ttl);
    break;
  }

  case MDNS_RECORDTYPE_A: {
    /* A record: IPv4 address
     * mdns_record_parse_a fills a sockaddr_in structure with the address
     */
    struct sockaddr_in addr_in;
    mdns_record_parse_a(data, size, record_offset, record_length, &addr_in);

    /* Convert network address to dotted decimal notation */
    uint8_t *bytes = (uint8_t *)&addr_in.sin_addr;
    safe_snprintf(discovery.ipv4, sizeof(discovery.ipv4), "%d.%d.%d.%d", bytes[0], bytes[1], bytes[2], bytes[3]);
    log_debug("mDNS A: %s -> %s (TTL: %u)", discovery.type, discovery.ipv4, ttl);
    break;
  }

  case MDNS_RECORDTYPE_AAAA: {
    /* AAAA record: IPv6 address
     * mdns_record_parse_aaaa fills a sockaddr_in6 structure with the address
     */
    struct sockaddr_in6 addr_in6;
    mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr_in6);

    /* Convert IPv6 address to colon-separated hex notation */
    const uint8_t *bytes = (const uint8_t *)&addr_in6.sin6_addr;
    safe_snprintf(discovery.ipv6, sizeof(discovery.ipv6),
                  "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", bytes[0], bytes[1],
                  bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
                  bytes[12], bytes[13], bytes[14], bytes[15]);
    log_debug("mDNS AAAA: %s -> %s (TTL: %u)", discovery.type, discovery.ipv6, ttl);
    break;
  }

  case MDNS_RECORDTYPE_TXT: {
    /* TXT record: service properties (key=value pairs)
     * For simplicity, we skip individual TXT parsing for now
     * Could be extended to extract and store specific properties
     */
    log_debug("mDNS TXT: %s (TTL: %u)", discovery.type, ttl);
    discovery.txt[0] = '\0';
    break;
  }

  default:
    log_debug("mDNS unknown record type: %u", rtype);
    return 0;
  }

  /* Call the user callback with discovered service */
  mdns->callback(&discovery, mdns->callback_data);

  (void)sock;
  (void)query_id;
  (void)rclass;
  (void)addrlen;
  (void)from;
  (void)name_length;
  return 0;
}

asciichat_error_t asciichat_mdns_query(asciichat_mdns_t *mdns, const char *service_type,
                                       asciichat_mdns_discovery_callback_fn callback, void *user_data) {
  if (!mdns || !service_type || !callback) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mDNS query parameters");
  }

  /* Validate service type is non-empty and has minimum length
   * Prevents underflow bug in mdns library when processing empty strings
   */
  size_t service_type_len = strlen(service_type);
  if (service_type_len == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Service type cannot be empty");
  }

  mdns->callback = callback;
  mdns->callback_data = user_data;

  log_info("Starting mDNS query for: %s", service_type);

  /* Send PTR query for service type (one-shot query)
   * PTR query discovers all instances of a service type
   * mdns_query_send returns the query ID for response filtering
   */
  int query_id = mdns_query_send(mdns->socket_fd, MDNS_RECORDTYPE_PTR, service_type, service_type_len, mdns->buffer,
                                 mdns->buffer_capacity, 0);

  if (query_id <= 0) {
    return SET_ERRNO(ERROR_NETWORK, "mDNS query send failed for %s (query_id=%d)", service_type, query_id);
  }

  mdns->query_id = (uint16_t)query_id;
  log_debug("mDNS query sent for service type: %s (query_id: %d)", service_type, query_id);

  return ASCIICHAT_OK;
}

asciichat_error_t asciichat_mdns_update(asciichat_mdns_t *mdns, int timeout_ms) {
  if (!mdns) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mDNS context is NULL");
  }

  /* Process incoming mDNS packets (responses from previous queries)
   * mdns_query_recv processes all records received since last call
   * The callback function (mdns_record_callback) is invoked for each record
   */
  int num_records =
      mdns_query_recv(mdns->socket_fd, mdns->buffer, mdns->buffer_capacity, mdns_record_callback, mdns, mdns->query_id);

  if (num_records < 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive mDNS query responses");
  }

  if (num_records > 0) {
    log_debug("Processed %d mDNS records", num_records);
  }

  (void)timeout_ms;
  return ASCIICHAT_OK;
}

int asciichat_mdns_get_socket(asciichat_mdns_t *mdns) {
  if (!mdns) {
    return -1;
  }
  return mdns->socket_fd;
}
