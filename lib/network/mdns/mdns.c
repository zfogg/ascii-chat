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
#include <ascii-chat/debug/named.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/util/time.h>
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
  bool advertising;
  _Atomic bool stop_advertising;
  asciichat_thread_t advertiser;
  char service_type[256], instance[256], host[256], query_service[256];
  char txt[16][512];
  mdns_record_t ptr, additional[18];
  size_t additional_count;
  uint16_t query_id; /**< Current query ID */
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

  log_dev("mDNS context initialized (socket: %d, buffer: %zu bytes)", mdns->socket_fd, mdns->buffer_capacity);

  // Register mDNS context for debugging
  NAMED_REGISTER_CONTEXT(mdns, "asciichat_mdns", "mdns_context", NULL);

  return mdns;
}

static void mdns_stop_advertising(asciichat_mdns_t *mdns) {
  if (!mdns->advertising)
    return;
  atomic_store(&mdns->stop_advertising, true);
  thread_join(&mdns->advertiser, NULL);
  mdns_goodbye_multicast(mdns->socket_fd, mdns->buffer, mdns->buffer_capacity, mdns->ptr, NULL, 0, mdns->additional,
                         mdns->additional_count);
  mdns->advertising = false;
}

static int mdns_service_callback(int sock, const struct sockaddr *from, size_t addrlen, mdns_entry_type_t entry,
                                 uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void *data,
                                 size_t size, size_t name_offset, size_t name_length, size_t record_offset,
                                 size_t record_length, void *user_data) {
  (void)ttl;
  (void)name_length;
  (void)record_offset;
  (void)record_length;
  asciichat_mdns_t *mdns = user_data;
  if (entry != MDNS_ENTRYTYPE_QUESTION || (rtype != MDNS_RECORDTYPE_PTR && rtype != MDNS_RECORDTYPE_ANY))
    return 0;
  char name[256] = {0};
  mdns_string_extract(data, size, &name_offset, name, sizeof(name) - 1);
  if (strcmp(name, mdns->service_type) != 0)
    return 0;
  uint8_t response[MDNS_BUFFER_SIZE];
  if (rclass & MDNS_UNICAST_RESPONSE)
    mdns_query_answer_unicast(sock, from, addrlen, response, sizeof(response), query_id, (mdns_record_type_t)rtype,
                              name, strlen(name), mdns->ptr, NULL, 0, mdns->additional, mdns->additional_count);
  else
    mdns_query_answer_multicast(sock, response, sizeof(response), mdns->ptr, NULL, 0, mdns->additional,
                                mdns->additional_count);
  return 0;
}

static void *mdns_advertiser(void *data) {
  asciichat_mdns_t *mdns = data;
  while (!atomic_load(&mdns->stop_advertising)) {
    struct pollfd fd = {.fd = mdns->socket_fd, .events = POLLIN};
    if (socket_poll(&fd, 1, 100 * NS_PER_MS_INT) > 0)
      mdns_socket_listen(mdns->socket_fd, mdns->buffer, mdns->buffer_capacity, mdns_service_callback, mdns);
  }
  return NULL;
}

void asciichat_mdns_destroy(asciichat_mdns_t *mdns) {
  if (!mdns) {
    return;
  }

  mdns_stop_advertising(mdns);
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

  if (!service->port || !service->name[0] || !service->type[0] || !service->host[0] || service->txt_count > 16 ||
      (service->txt_count && !service->txt_records))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mDNS service records");
  if (strlen(service->name) > 55 || strlen(service->type) > 180 || strlen(service->host) > 240)
    return SET_ERRNO(ERROR_INVALID_PARAM, "mDNS service name is too long");
  for (size_t i = 0; i < service->txt_count; ++i)
    if (!service->txt_records[i] || strlen(service->txt_records[i]) >= sizeof(mdns->txt[i]))
      return SET_ERRNO(ERROR_INVALID_PARAM, "mDNS TXT record is missing or too long");
  mdns_stop_advertising(mdns);
  mdns_socket_close(mdns->socket_fd);
  struct sockaddr_in listen_address = {.sin_family = AF_INET, .sin_port = htons(MDNS_PORT)};
  mdns->socket_fd = mdns_socket_open_ipv4(&listen_address);
  if (mdns->socket_fd < 0)
    return SET_ERRNO(ERROR_NETWORK_BIND, "Cannot listen for mDNS service queries");
  safe_snprintf(mdns->service_type, sizeof(mdns->service_type), "%s%s", service->type,
                strstr(service->type, ".local") ? (service->type[strlen(service->type) - 1] == '.' ? "" : ".")
                                                : ".local.");
  safe_snprintf(mdns->instance, sizeof(mdns->instance), "%s-%u.%s", service->name, service->port, mdns->service_type);
  safe_snprintf(mdns->host, sizeof(mdns->host), "%s%s", service->host,
                strstr(service->host, ".local") ? (service->host[strlen(service->host) - 1] == '.' ? "" : ".")
                                                : ".local.");
  mdns_string_t type = {mdns->service_type, strlen(mdns->service_type)};
  mdns_string_t instance = {mdns->instance, strlen(mdns->instance)};
  mdns->ptr = (mdns_record_t){.name = type, .type = MDNS_RECORDTYPE_PTR, .data.ptr.name = instance};
  mdns->additional[0] = (mdns_record_t){.name = instance,
                                        .type = MDNS_RECORDTYPE_SRV,
                                        .data.srv = {.name = {mdns->host, strlen(mdns->host)}, .port = service->port}};
  mdns->additional_count = 1;
  socket_t route = socket_create("mdns_route", AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (route != INVALID_SOCKET_VALUE) {
    struct sockaddr_in multicast = {
        .sin_family = AF_INET, .sin_port = htons(MDNS_PORT), .sin_addr.s_addr = htonl(0xe00000fb)};
    struct sockaddr_in local = {0};
    socklen_t length = sizeof(local);
    if (socket_connect(route, (struct sockaddr *)&multicast, sizeof(multicast)) == 0 &&
        socket_getsockname(route, (struct sockaddr *)&local, &length) == 0)
      mdns->additional[mdns->additional_count++] =
          (mdns_record_t){.name = {mdns->host, strlen(mdns->host)}, .type = MDNS_RECORDTYPE_A, .data.a.addr = local};
    socket_close(route);
  }
  for (size_t i = 0; i < service->txt_count; ++i) {
    SAFE_STRNCPY(mdns->txt[i], service->txt_records[i], sizeof(mdns->txt[i]));
    char *value = strchr(mdns->txt[i], '=');
    if (value)
      *value++ = '\0';
    mdns->additional[mdns->additional_count++] = (mdns_record_t){
        .name = instance,
        .type = MDNS_RECORDTYPE_TXT,
        .data.txt = {.key = {mdns->txt[i], strlen(mdns->txt[i])}, .value = {value, value ? strlen(value) : 0}}};
  }
  if (mdns_announce_multicast(mdns->socket_fd, mdns->buffer, mdns->buffer_capacity, mdns->ptr, NULL, 0,
                              mdns->additional, mdns->additional_count) < 0)
    return SET_ERRNO(ERROR_NETWORK, "Cannot announce mDNS service");
  atomic_store(&mdns->stop_advertising, false);
  if (acthread_create(&mdns->advertiser, "mdns_advertiser", mdns_advertiser, mdns) != 0)
    return SET_ERRNO(ERROR_THREAD, "Cannot start mDNS advertiser");
  mdns->advertising = true;
  return ASCIICHAT_OK;
}

asciichat_error_t asciichat_mdns_unadvertise(asciichat_mdns_t *mdns, const char *service_name) {
  if (!mdns || !service_name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mDNS context or service name is NULL");
  }

  log_info("Stopped advertising service: %s", service_name);

  mdns_stop_advertising(mdns);

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

  // A complete SRV answer binds one instance to a host and port. PTR/A/TXT
  // records alone are never exposed as selectable, port-zero servers.
  if ((entry != MDNS_ENTRYTYPE_ANSWER && entry != MDNS_ENTRYTYPE_ADDITIONAL) || rtype != MDNS_RECORDTYPE_SRV || !ttl ||
      record_length < 7)
    return 0;
  asciichat_mdns_discovery_t discovery = {0};
  discovery.ttl = ttl;
  size_t offset = name_offset;
  mdns_string_extract(data, size, &offset, discovery.type, sizeof(discovery.type) - 1);
  const char *suffix = strstr(discovery.type, mdns->query_service);
  if (!suffix || suffix == discovery.type || suffix[-1] != '.')
    return 0;
  safe_snprintf(discovery.name, sizeof(discovery.name), "%.*s", (int)(suffix - discovery.type - 1), discovery.type);
  mdns_record_srv_t srv =
      mdns_record_parse_srv(data, size, record_offset, record_length, discovery.host, sizeof(discovery.host) - 1);
  if (!srv.port || !srv.name.length)
    return 0;
  discovery.port = srv.port;
  // Direct LAN responders send from the interface on which they are reachable.
  // Retain the advertised hostname for peers without a usable source address.
  if (from && from->sa_family == AF_INET && addrlen >= sizeof(struct sockaddr_in)) {
    const uint8_t *ip = (const uint8_t *)&((const struct sockaddr_in *)from)->sin_addr;
    safe_snprintf(discovery.ipv4, sizeof(discovery.ipv4), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  }
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
  if (mdns->advertising)
    return SET_ERRNO(ERROR_INVALID_STATE, "Use a separate mDNS context for discovery while advertising");

  /* Validate service type is non-empty and has minimum length
   * Prevents underflow bug in mdns library when processing empty strings
   */
  size_t service_type_len = strlen(service_type);
  if (service_type_len == 0 || service_type_len > 253) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Service type cannot be empty");
  }

  safe_snprintf(mdns->query_service, sizeof(mdns->query_service), "%s%s", service_type,
                service_type[service_type_len - 1] == '.' ? "" : ".");
  mdns->callback = callback;
  mdns->callback_data = user_data;

  log_info("Starting mDNS query for: %s", service_type);

  /* Send PTR query for service type (one-shot query)
   * PTR query discovers all instances of a service type
   * mdns_query_send returns the query ID for response filtering
   */
  int query_id = mdns_query_send(mdns->socket_fd, MDNS_RECORDTYPE_PTR, service_type, service_type_len, mdns->buffer,
                                 mdns->buffer_capacity, 0);

  if (query_id < 0) {
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
  if (mdns->advertising)
    return ASCIICHAT_OK;

  /* Process incoming mDNS packets (responses from previous queries)
   * mdns_query_recv processes all records received since last call
   * The callback function (mdns_record_callback) is invoked for each record
   */
  struct pollfd fd = {.fd = mdns->socket_fd, .events = POLLIN};
  if (socket_poll(&fd, 1, (int64_t)timeout_ms * NS_PER_MS_INT) <= 0)
    return ASCIICHAT_OK;
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
