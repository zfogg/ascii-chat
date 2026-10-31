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
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h> /* For struct sockaddr_in, sockaddr_in6 */
#endif
#ifdef __APPLE__
#include <dns_sd.h>
typedef struct {
  asciichat_mdns_t *owner;
  DNSServiceRef ref, address_ref;
  char name[256];
  asciichat_mdns_discovery_t found;
} bonjour_resolver_t;
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
  asciichat_mdns_discovery_t discovered[64];
  size_t discovered_count;
  uint16_t query_id; /**< Current query ID */
#ifdef __APPLE__
  DNSServiceRef dns_service_ref, browser;
  bonjour_resolver_t resolvers[64];
  size_t resolver_count;
  DNSServiceErrorType query_error;
#endif
};

#ifdef __APPLE__
static void bonjour_query_close(asciichat_mdns_t *mdns) {
  for (size_t i = 0; i < mdns->resolver_count; ++i)
    if (mdns->resolvers[i].ref) {
      if (mdns->resolvers[i].address_ref)
        DNSServiceRefDeallocate(mdns->resolvers[i].address_ref);
      DNSServiceRefDeallocate(mdns->resolvers[i].ref);
    }
  mdns->resolver_count = 0;
  if (mdns->browser)
    DNSServiceRefDeallocate(mdns->browser);
  mdns->browser = NULL;
  mdns->query_error = kDNSServiceErr_NoError;
}

static void bonjour_record_error(asciichat_mdns_t *mdns, DNSServiceErrorType error) {
  // An individual record may disappear while the remaining services are resolved.
  if (error != kDNSServiceErr_NoSuchName && error != kDNSServiceErr_NoSuchRecord)
    mdns->query_error = error;
}

static void bonjour_address(DNSServiceRef ref, DNSServiceFlags flags, uint32_t interface_index,
                            DNSServiceErrorType error, const char *hostname, const struct sockaddr *address,
                            uint32_t ttl, void *context) {
  bonjour_resolver_t *resolver = context;
  if (error != kDNSServiceErr_NoError) {
    bonjour_record_error(resolver->owner, error);
    return;
  }
  if (!(flags & kDNSServiceFlagsAdd) || !address || address->sa_family != AF_INET)
    return;
  const uint8_t *ip = (const uint8_t *)&((const struct sockaddr_in *)address)->sin_addr;
  safe_snprintf(resolver->found.ipv4, sizeof(resolver->found.ipv4), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  resolver->found.ttl = ttl;
  resolver->owner->callback(&resolver->found, resolver->owner->callback_data);
  (void)ref;
  (void)interface_index;
  (void)hostname;
}

static void bonjour_resolved(DNSServiceRef ref, DNSServiceFlags flags, uint32_t interface_index,
                             DNSServiceErrorType error, const char *fullname, const char *host, uint16_t port,
                             uint16_t txt_length, const unsigned char *txt, void *context) {
  bonjour_resolver_t *resolver = context;
  asciichat_mdns_t *mdns = resolver->owner;
  if (error != kDNSServiceErr_NoError) {
    bonjour_record_error(mdns, error);
    return;
  }
  asciichat_mdns_discovery_t *found = &resolver->found;
  SAFE_STRNCPY(found->name, resolver->name, sizeof(found->name));
  SAFE_STRNCPY(found->type, fullname, sizeof(found->type));
  SAFE_STRNCPY(found->host, host, sizeof(found->host));
  found->port = ntohs(port);
  uint8_t length = 0;
  const void *session = TXTRecordGetValuePtr(txt_length, txt, "session_string", &length);
  if (session)
    safe_snprintf(found->txt, sizeof(found->txt), "session_string=%.*s", (int)length, (const char *)session);
  if (resolver->address_ref)
    return;
  error = DNSServiceGetAddrInfo(&resolver->address_ref, 0, interface_index, kDNSServiceProtocol_IPv4, host,
                                bonjour_address, resolver);
  if (error != kDNSServiceErr_NoError)
    bonjour_record_error(mdns, error);
  (void)ref;
  (void)flags;
  (void)interface_index;
}

static void bonjour_browsed(DNSServiceRef ref, DNSServiceFlags flags, uint32_t interface_index,
                            DNSServiceErrorType error, const char *name, const char *type, const char *domain,
                            void *context) {
  asciichat_mdns_t *mdns = context;
  if (error != kDNSServiceErr_NoError) {
    mdns->query_error = error;
    return;
  }
  if (!(flags & kDNSServiceFlagsAdd) || mdns->resolver_count == 64)
    return;
  bonjour_resolver_t *resolver = &mdns->resolvers[mdns->resolver_count];
  memset(resolver, 0, sizeof(*resolver));
  resolver->owner = mdns;
  SAFE_STRNCPY(resolver->name, name, sizeof(resolver->name));
  error = DNSServiceResolve(&resolver->ref, 0, interface_index, name, type, domain, bonjour_resolved, resolver);
  if (error == kDNSServiceErr_NoError)
    ++mdns->resolver_count;
  else
    bonjour_record_error(mdns, error);
  (void)ref;
}

static asciichat_error_t bonjour_update(asciichat_mdns_t *mdns, int timeout_ms) {
  DNSServiceRef refs[129];
  struct pollfd fds[129];
  size_t count = 0;
  if (mdns->browser)
    refs[count++] = mdns->browser;
  for (size_t i = 0; i < mdns->resolver_count; ++i) {
    refs[count++] = mdns->resolvers[i].ref;
    if (mdns->resolvers[i].address_ref)
      refs[count++] = mdns->resolvers[i].address_ref;
  }
  for (size_t i = 0; i < count; ++i)
    fds[i] = (struct pollfd){.fd = DNSServiceRefSockFD(refs[i]), .events = POLLIN};
  if (count && socket_poll(fds, count, (int64_t)timeout_ms * NS_PER_MS_INT) > 0) {
    for (size_t i = 0; i < count; ++i) {
      if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        mdns->query_error = fds[i].revents & POLLNVAL ? kDNSServiceErr_BadReference : kDNSServiceErr_ServiceNotRunning;
        continue;
      }
      if (!(fds[i].revents & POLLIN))
        continue;
      DNSServiceErrorType error = DNSServiceProcessResult(refs[i]);
      if (error != kDNSServiceErr_NoError)
        mdns->query_error = error;
    }
  }
  if (mdns->query_error != kDNSServiceErr_NoError)
    return SET_ERRNO(ERROR_NETWORK, "Bonjour discovery failed (error %d)", mdns->query_error);
  return ASCIICHAT_OK;
}
#endif

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
#ifdef __APPLE__
  if (mdns->dns_service_ref) {
    DNSServiceRefDeallocate(mdns->dns_service_ref);
    mdns->dns_service_ref = NULL;
  }
#else
  atomic_store(&mdns->stop_advertising, true);
  thread_join(&mdns->advertiser, NULL);
  mdns_goodbye_multicast(mdns->socket_fd, mdns->buffer, mdns->buffer_capacity, mdns->ptr, NULL, 0, mdns->additional,
                         mdns->additional_count);
#endif
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
#ifdef __APPLE__
  bonjour_query_close(mdns);
#endif
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
#ifdef __APPLE__
  TXTRecordRef txt_record;
  TXTRecordCreate(&txt_record, 0, NULL);
  char regtype[256];
  SAFE_STRNCPY(regtype, service->type, sizeof(regtype));
  char *local_suffix = strstr(regtype, ".local");
  if (local_suffix)
    *local_suffix = '\0';
  size_t regtype_length = strlen(regtype);
  if (regtype_length && regtype[regtype_length - 1] == '.')
    regtype[regtype_length - 1] = '\0';
  for (size_t i = 0; i < service->txt_count; ++i) {
    const char *separator = strchr(service->txt_records[i], '=');
    size_t key_length = separator ? (size_t)(separator - service->txt_records[i]) : strlen(service->txt_records[i]);
    if (!key_length || key_length >= 256) {
      TXTRecordDeallocate(&txt_record);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mDNS TXT record key");
    }
    char key[256];
    safe_snprintf(key, sizeof(key), "%.*s", (int)key_length, service->txt_records[i]);
    const void *value = separator ? separator + 1 : NULL;
    size_t value_length = separator ? strlen(separator + 1) : 0;
    if (value_length > UINT8_MAX ||
        TXTRecordSetValue(&txt_record, key, (uint8_t)value_length, value) != kDNSServiceErr_NoError) {
      TXTRecordDeallocate(&txt_record);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mDNS TXT record value");
    }
  }
  char instance_name[64];
  safe_snprintf(instance_name, sizeof(instance_name), "%s-%u", service->name, service->port);
  DNSServiceErrorType error = DNSServiceRegister(
      &mdns->dns_service_ref, 0, kDNSServiceInterfaceIndexAny, instance_name, regtype, NULL, NULL, htons(service->port),
      TXTRecordGetLength(&txt_record), TXTRecordGetBytesPtr(&txt_record), NULL, NULL);
  TXTRecordDeallocate(&txt_record);
  if (error != kDNSServiceErr_NoError)
    return SET_ERRNO(ERROR_NETWORK, "Cannot register mDNS service with Bonjour (error %d)", error);
  mdns->advertising = true;
  return ASCIICHAT_OK;
#else
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
  asciichat_errno_scope_t route_scope = errno_checkpoint();
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
  errno_scope_end(route_scope, ASCIICHAT_ERRNO_DISMISSED);
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
#endif
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
  if ((entry != MDNS_ENTRYTYPE_ANSWER && entry != MDNS_ENTRYTYPE_ADDITIONAL) ||
      (rtype != MDNS_RECORDTYPE_SRV && rtype != MDNS_RECORDTYPE_TXT) || !ttl)
    return 0;
  asciichat_mdns_discovery_t discovery = {0};
  discovery.ttl = ttl;
  size_t offset = name_offset;
  mdns_string_extract(data, size, &offset, discovery.type, sizeof(discovery.type) - 1);
  const char *suffix = strstr(discovery.type, mdns->query_service);
  if (!suffix || suffix == discovery.type || suffix[-1] != '.' || strcmp(suffix, mdns->query_service) != 0)
    return 0;
  safe_snprintf(discovery.name, sizeof(discovery.name), "%.*s", (int)(suffix - discovery.type - 1), discovery.type);
  size_t index = 0;
  while (index < mdns->discovered_count && strcmp(mdns->discovered[index].type, discovery.type) != 0)
    ++index;
  if (index == mdns->discovered_count) {
    if (index == sizeof(mdns->discovered) / sizeof(mdns->discovered[0]))
      return 0;
    mdns->discovered[index] = discovery;
    ++mdns->discovered_count;
  }
  asciichat_mdns_discovery_t *cached = &mdns->discovered[index];
  if (rtype == MDNS_RECORDTYPE_TXT) {
    mdns_record_txt_t records[16];
    size_t count = mdns_record_parse_txt(data, size, record_offset, record_length, records, 16);
    for (size_t i = 0; i < count; ++i) {
      if (records[i].key.length == strlen("session_string") &&
          memcmp(records[i].key.str, "session_string", records[i].key.length) == 0 && records[i].value.length < 256)
        safe_snprintf(cached->txt, sizeof(cached->txt), "session_string=%.*s", (int)records[i].value.length,
                      records[i].value.str);
    }
    if (cached->port)
      mdns->callback(cached, mdns->callback_data);
    return 0;
  }
  if (record_length < 7)
    return 0;
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
  SAFE_STRNCPY(discovery.txt, cached->txt, sizeof(discovery.txt));
  *cached = discovery;
  mdns->callback(cached, mdns->callback_data);

  (void)sock;
  (void)query_id;
  (void)rclass;
  (void)addrlen;
  (void)from;
  (void)name_length;
  return 0;
}

static int mdns_send_query_on_interface(asciichat_mdns_t *mdns, const char *service_type, size_t service_type_len,
                                        struct in_addr address) {
  if (setsockopt(mdns->socket_fd, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&address, sizeof(address)) != 0)
    return -1;
  return mdns_query_send(mdns->socket_fd, MDNS_RECORDTYPE_PTR, service_type, service_type_len, mdns->buffer,
                         mdns->buffer_capacity, 0);
}

static int mdns_send_query_all_interfaces(asciichat_mdns_t *mdns, const char *service_type, size_t service_type_len) {
  int sent = 0;
#ifdef _WIN32
  ULONG size = 0;
  if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST, NULL,
                           NULL, &size) == ERROR_BUFFER_OVERFLOW) {
    IP_ADAPTER_ADDRESSES *adapters = SAFE_MALLOC(size, IP_ADAPTER_ADDRESSES *);
    if (adapters) {
      if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST,
                               NULL, adapters, &size) == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES *adapter = adapters; adapter; adapter = adapter->Next) {
          if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
              adapter->NoMulticast)
            continue;
          for (IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET)
              continue;
            struct in_addr address = ((struct sockaddr_in *)unicast->Address.lpSockaddr)->sin_addr;
            if (mdns_send_query_on_interface(mdns, service_type, service_type_len, address) >= 0)
              ++sent;
          }
        }
      }
      SAFE_FREE(adapters);
    }
  }
#else
  struct ifaddrs *interfaces = NULL;
  if (getifaddrs(&interfaces) == 0) {
    for (struct ifaddrs *interface = interfaces; interface; interface = interface->ifa_next) {
      if (!interface->ifa_addr || interface->ifa_addr->sa_family != AF_INET || !(interface->ifa_flags & IFF_UP) ||
          !(interface->ifa_flags & IFF_MULTICAST) || (interface->ifa_flags & IFF_LOOPBACK))
        continue;
      struct in_addr address = ((struct sockaddr_in *)interface->ifa_addr)->sin_addr;
      if (mdns_send_query_on_interface(mdns, service_type, service_type_len, address) >= 0)
        ++sent;
    }
    freeifaddrs(interfaces);
  }
#endif
  if (!sent)
    return mdns_query_send(mdns->socket_fd, MDNS_RECORDTYPE_PTR, service_type, service_type_len, mdns->buffer,
                           mdns->buffer_capacity, 0);
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
  mdns->discovered_count = 0;

#ifdef __APPLE__
  bonjour_query_close(mdns);
  char regtype[256];
  SAFE_STRNCPY(regtype, service_type, sizeof(regtype));
  char *domain = strstr(regtype, ".local");
  if (domain)
    *domain = '\0';
  size_t length = strlen(regtype);
  if (length && regtype[length - 1] == '.')
    regtype[length - 1] = '\0';
  DNSServiceErrorType error =
      DNSServiceBrowse(&mdns->browser, 0, kDNSServiceInterfaceIndexAny, regtype, "local.", bonjour_browsed, mdns);
  if (error != kDNSServiceErr_NoError)
    return SET_ERRNO(ERROR_NETWORK, "Cannot browse mDNS services with Bonjour (error %d)", error);
  return ASCIICHAT_OK;
#else
  // Use the standard mDNS port so responders multicast their answers. Some
  // system mDNS responders do not return QU answers to an ephemeral source
  // port, and multicast answers are also visible across all active adapters.
  mdns_socket_close(mdns->socket_fd);
  struct sockaddr_in listen_address = {.sin_family = AF_INET, .sin_port = htons(MDNS_PORT)};
  mdns->socket_fd = mdns_socket_open_ipv4(&listen_address);
  if (mdns->socket_fd < 0)
    return SET_ERRNO(ERROR_NETWORK_BIND, "Cannot listen for mDNS query responses");

  log_info("Starting mDNS query for: %s", service_type);

  /* Send PTR query for service type (one-shot query)
   * PTR query discovers all instances of a service type
   * mdns_query_send returns the query ID for response filtering
   */
  int query_id = mdns_send_query_all_interfaces(mdns, service_type, service_type_len);

  if (query_id < 0) {
    return SET_ERRNO(ERROR_NETWORK, "mDNS query send failed for %s (query_id=%d)", service_type, query_id);
  }

  mdns->query_id = (uint16_t)query_id;
  log_debug("mDNS query sent for service type: %s (query_id: %d)", service_type, query_id);

  return ASCIICHAT_OK;
#endif
}

asciichat_error_t asciichat_mdns_update(asciichat_mdns_t *mdns, int timeout_ms) {
  if (!mdns) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mDNS context is NULL");
  }
  if (mdns->advertising)
    return ASCIICHAT_OK;

#ifdef __APPLE__
  return bonjour_update(mdns, timeout_ms);
#else
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
#endif
}

int asciichat_mdns_get_socket(asciichat_mdns_t *mdns) {
  if (!mdns) {
    return -1;
  }
#ifdef __APPLE__
  if (mdns->browser)
    return DNSServiceRefSockFD(mdns->browser);
#endif
  return mdns->socket_fd;
}
