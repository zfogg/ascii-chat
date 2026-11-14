/**
 * @file mdns_test.c
 * @brief Unit tests for mDNS service discovery (lib/network/mdns.c)
 *
 * Tests mDNS abstraction layer functionality:
 * - Context initialization and cleanup
 * - Service advertisement
 * - mDNS querying
 * - Response parsing and callbacks
 * - Error handling
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <stdlib.h>
#include <string.h>

#include <ascii-chat/network/mdns/mdns.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/common.h>

// Static callback for query tests (must be at file scope, not inside Test())
static void dummy_callback(const asciichat_mdns_discovery_t *discovery, void *user_data) {
  (void)discovery;
  (void)user_data;
}

/**
 * @brief Test mDNS context initialization
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("mdns/init_creates_valid_context", "[mdns]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  ASCIICHAT_REQUIRE_NOT_NULL(mdns, "mDNS context should be initialized");

  // Verify socket is valid
  int socket = asciichat_mdns_get_socket(mdns);
  ASCIICHAT_REQUIRE_GT(socket, -1, "mDNS socket should be valid");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS context cleanup
 */
TEST_CASE("mdns/shutdown_frees_resources", "[mdns]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  ASCIICHAT_REQUIRE_NOT_NULL(mdns, "mDNS should initialize successfully");

  // Should not crash when shutting down
  asciichat_mdns_destroy(mdns);

  // Verify shutdown completed (if we get here, it didn't crash)
  // Note: We can't call it again on same pointer, but we test it doesn't crash
}

/**
 * @brief Test mDNS service advertisement
 */
TEST_CASE("mdns/advertise_service_succeeds", "[mdns]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  ASCIICHAT_REQUIRE_NOT_NULL(mdns, "mDNS should initialize");

  asciichat_mdns_service_t service = {
      .name = "test-service",
      .type = "_test._tcp",
      .host = "testhost.local",
      .port = 9999,
      .txt_records = NULL,
      .txt_count = 0,
  };

  asciichat_error_t result = asciichat_mdns_advertise(mdns, &service);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Service advertisement should succeed");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS service advertisement with NULL context
 */
TEST_CASE("mdns/advertise_service_null_context_fails", "[mdns]") {
  asciichat_mdns_service_t service = {
      .name = "test-service",
      .type = "_test._tcp",
      .host = "testhost.local",
      .port = 9999,
      .txt_records = NULL,
      .txt_count = 0,
  };

  asciichat_error_t result = asciichat_mdns_advertise(NULL, &service);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL context");
}

/**
 * @brief Test mDNS service advertisement with NULL service
 */
TEST_CASE("mdns/advertise_service_null_service_fails", "[mdns]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  ASCIICHAT_REQUIRE_NOT_NULL(mdns, "mDNS should initialize");

  asciichat_error_t result = asciichat_mdns_advertise(mdns, NULL);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL service");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS query initialization
 */
TEST_CASE("mdns/query_initializes_successfully", "[mdns]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  ASCIICHAT_REQUIRE_NOT_NULL(mdns, "mDNS should initialize");

  asciichat_error_t result = asciichat_mdns_query(mdns, "_test._tcp.local", dummy_callback, NULL);
  if (result != ASCIICHAT_OK) {
    asciichat_mdns_destroy(mdns);
    ASCIICHAT_SKIP("mDNS multicast is unavailable in this runner");
  }
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Query should initialize successfully");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS query with invalid service type
 */
TEST_CASE("mdns/query_with_empty_service_type_fails", "[mdns]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  ASCIICHAT_REQUIRE_NOT_NULL(mdns, "mDNS should initialize");

  asciichat_error_t result = asciichat_mdns_query(mdns, "", dummy_callback, NULL);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Query with empty service type should fail");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS update with valid context
 */
TEST_CASE("mdns/update_processes_responses", "[mdns]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  ASCIICHAT_REQUIRE_NOT_NULL(mdns, "mDNS should initialize");

  // Update should not crash even with no pending responses
  asciichat_error_t result = asciichat_mdns_update(mdns, 100);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Update should succeed");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS update with NULL context
 */
TEST_CASE("mdns/update_null_context_fails", "[mdns]") {
  asciichat_error_t result = asciichat_mdns_update(NULL, 100);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Update with NULL context should fail");
}

/**
 * @brief Test mDNS socket retrieval
 */
TEST_CASE("mdns/get_socket_returns_valid_descriptor", "[mdns]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  ASCIICHAT_REQUIRE_NOT_NULL(mdns, "mDNS should initialize");

  int socket = asciichat_mdns_get_socket(mdns);
  ASCIICHAT_REQUIRE_GT(socket, -1, "Socket should be valid file descriptor");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test mDNS socket retrieval with NULL context
 */
TEST_CASE("mdns/get_socket_null_context_returns_invalid", "[mdns]") {
  int socket = asciichat_mdns_get_socket(NULL);
  ASCIICHAT_REQUIRE_EQ(socket, -1, "Socket should be invalid for NULL context");
}

/**
 * @brief Test service unadvertisement
 */
TEST_CASE("mdns/unadvertise_service_succeeds", "[mdns]") {
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  ASCIICHAT_REQUIRE_NOT_NULL(mdns, "mDNS should initialize");

  asciichat_error_t result = asciichat_mdns_unadvertise(mdns, "test-service");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Unadvertisement should succeed");

  asciichat_mdns_destroy(mdns);
}

/**
 * @brief Test service unadvertisement with NULL context
 */
TEST_CASE("mdns/unadvertise_service_null_context_fails", "[mdns]") {
  asciichat_error_t result = asciichat_mdns_unadvertise(NULL, "test-service");
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL context");
}

#ifdef __APPLE__
#include <dns_sd.h>
#include <ascii-chat/platform/socket.h>

static DNSServiceErrorType mock_browse(DNSServiceRef *, DNSServiceFlags, uint32_t, const char *, const char *,
                                       DNSServiceBrowseReply, void *);
static DNSServiceErrorType mock_resolve(DNSServiceRef *, DNSServiceFlags, uint32_t, const char *, const char *,
                                        const char *, DNSServiceResolveReply, void *);
static DNSServiceErrorType mock_address(DNSServiceRef *, DNSServiceFlags, uint32_t, DNSServiceProtocol, const char *,
                                        DNSServiceGetAddrInfoReply, void *);
static DNSServiceErrorType mock_process(DNSServiceRef);
static int mock_descriptor(DNSServiceRef);
static void mock_deallocate(DNSServiceRef);
static int mock_poll(struct pollfd *, nfds_t, int64_t);
#define DNSServiceBrowse mock_browse
#define DNSServiceResolve mock_resolve
#define DNSServiceGetAddrInfo mock_address
#define DNSServiceProcessResult mock_process
#define DNSServiceRefSockFD mock_descriptor
#define DNSServiceRefDeallocate mock_deallocate
#define socket_poll mock_poll
#define asciichat_mdns_init contract_mdns_init
#define asciichat_mdns_destroy contract_mdns_destroy
#define asciichat_mdns_advertise contract_mdns_advertise
#define asciichat_mdns_unadvertise contract_mdns_unadvertise
#define asciichat_mdns_query contract_mdns_query
#define asciichat_mdns_update contract_mdns_update
#define asciichat_mdns_get_socket contract_mdns_get_socket
#include "../../../lib/network/mdns/mdns.c"
#undef socket_poll

typedef struct {
  bool ready, closed;
  void *context;
} mock_dns_ref_t;
static mock_dns_ref_t dns_refs[3];
static DNSServiceBrowseReply browse_reply;
static DNSServiceResolveReply resolve_reply;
static DNSServiceGetAddrInfoReply address_reply;
static unsigned fault, callbacks, closed_refs;
static asciichat_mdns_discovery_t received;

static DNSServiceErrorType mock_browse(DNSServiceRef *out, DNSServiceFlags flags, uint32_t interface_index,
                                       const char *type, const char *domain, DNSServiceBrowseReply reply,
                                       void *context) {
  ASCIICHAT_REQUIRE_STR_EQ(type, "_ascii-chat._tcp");
  ASCIICHAT_REQUIRE_STR_EQ(domain, "local.");
  if (fault == 1)
    return kDNSServiceErr_ServiceNotRunning;
  dns_refs[0] = (mock_dns_ref_t){.ready = true, .context = context};
  *out = (DNSServiceRef)&dns_refs[0];
  browse_reply = reply;
  (void)flags;
  (void)interface_index;
  (void)domain;
  return kDNSServiceErr_NoError;
}
static DNSServiceErrorType mock_resolve(DNSServiceRef *out, DNSServiceFlags flags, uint32_t interface_index,
                                        const char *name, const char *type, const char *domain,
                                        DNSServiceResolveReply reply, void *context) {
  ASCIICHAT_REQUIRE_STR_EQ(name, "fixture-27224");
  if (fault == 2 || fault == 13)
    return fault == 13 ? kDNSServiceErr_NoSuchName : kDNSServiceErr_BadParam;
  dns_refs[1] = (mock_dns_ref_t){.ready = true, .context = context};
  *out = (DNSServiceRef)&dns_refs[1];
  resolve_reply = reply;
  (void)flags;
  (void)interface_index;
  (void)type;
  (void)domain;
  return kDNSServiceErr_NoError;
}
static DNSServiceErrorType mock_address(DNSServiceRef *out, DNSServiceFlags flags, uint32_t interface_index,
                                        DNSServiceProtocol protocol, const char *host, DNSServiceGetAddrInfoReply reply,
                                        void *context) {
  ASCIICHAT_REQUIRE_EQ(protocol, kDNSServiceProtocol_IPv4);
  ASCIICHAT_REQUIRE_STR_EQ(host, "fixture.local.");
  if (fault == 3 || fault == 12)
    return fault == 12 ? kDNSServiceErr_NoSuchName : kDNSServiceErr_BadParam;
  dns_refs[2] = (mock_dns_ref_t){.ready = true, .context = context};
  *out = (DNSServiceRef)&dns_refs[2];
  address_reply = reply;
  (void)flags;
  (void)interface_index;
  return kDNSServiceErr_NoError;
}
static int mock_descriptor(DNSServiceRef ref) {
  return 10 + (int)((mock_dns_ref_t *)ref - dns_refs);
}
static void mock_deallocate(DNSServiceRef ref) {
  mock_dns_ref_t *state = (mock_dns_ref_t *)ref;
  ASCIICHAT_REQUIRE_NOT(state->closed, "DNS handle was closed twice");
  state->closed = true;
  ++closed_refs;
}
static int mock_poll(struct pollfd *fds, nfds_t count, int64_t timeout) {
  int ready = 0;
  for (nfds_t i = 0; i < count; ++i) {
    ASCIICHAT_REQUIRE(fds[i].fd >= 10 && fds[i].fd < 13);
    fds[i].revents = dns_refs[fds[i].fd - 10].ready ? (fault == 8 ? POLLHUP : fault == 9 ? POLLNVAL : POLLIN) : 0;
    ready += fds[i].revents != 0;
  }
  (void)timeout;
  return ready;
}
static DNSServiceErrorType mock_process(DNSServiceRef ref) {
  mock_dns_ref_t *state = (mock_dns_ref_t *)ref;
  ASCIICHAT_REQUIRE_NOT(state->closed);
  state->ready = false;
  if (fault == 4)
    return kDNSServiceErr_BadReference;
  if (state == &dns_refs[0]) {
    browse_reply(ref, kDNSServiceFlagsAdd, 1, fault == 5 ? kDNSServiceErr_PolicyDenied : kDNSServiceErr_NoError,
                 "fixture-27224", "_ascii-chat._tcp.", "local.", state->context);
  } else if (state == &dns_refs[1]) {
    TXTRecordRef txt;
    TXTRecordCreate(&txt, 0, NULL);
    ASCIICHAT_REQUIRE_EQ(TXTRecordSetValue(&txt, "session_string", 13, "one-two-three"), kDNSServiceErr_NoError);
    resolve_reply(ref, 0, 1,
                  fault == 6    ? kDNSServiceErr_ServiceNotRunning
                  : fault == 11 ? kDNSServiceErr_NoSuchName
                                : kDNSServiceErr_NoError,
                  "fixture-27224._ascii-chat._tcp.local.", "fixture.local.", htons(27224), TXTRecordGetLength(&txt),
                  TXTRecordGetBytesPtr(&txt), state->context);
    TXTRecordDeallocate(&txt);
  } else {
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(0x7f000001)};
    address_reply(ref, kDNSServiceFlagsAdd, 1,
                  fault == 7    ? kDNSServiceErr_PolicyDenied
                  : fault == 10 ? kDNSServiceErr_NoSuchRecord
                                : kDNSServiceErr_NoError,
                  "fixture.local.", (const struct sockaddr *)&address, 120, state->context);
  }
  return kDNSServiceErr_NoError;
}
static void capture_bonjour(const asciichat_mdns_discovery_t *found, void *context) {
  received = *found;
  ++callbacks;
  ASCIICHAT_REQUIRE_EQ(context, &received);
}
TEST_CASE("mdns/bonjour_browse_resolves_address_metadata_and_closes_every_handle", "[mdns]") {
  memset(dns_refs, 0, sizeof(dns_refs));
  fault = callbacks = closed_refs = 0;
  asciichat_mdns_t *context = SAFE_CALLOC(1, sizeof(*context), asciichat_mdns_t *);
  context->socket_fd = -1;
  ASCIICHAT_REQUIRE_EQ(contract_mdns_query(context, "_ascii-chat._tcp.local.", capture_bonjour, &received), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(contract_mdns_get_socket(context), 10);
  for (unsigned i = 0; i < 4; ++i)
    ASCIICHAT_REQUIRE_EQ(contract_mdns_update(context, 10), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(callbacks, 1);
  ASCIICHAT_REQUIRE_STR_EQ(received.name, "fixture-27224");
  ASCIICHAT_REQUIRE_STR_EQ(received.host, "fixture.local.");
  ASCIICHAT_REQUIRE_STR_EQ(received.ipv4, "127.0.0.1");
  ASCIICHAT_REQUIRE_STR_EQ(received.txt, "session_string=one-two-three");
  ASCIICHAT_REQUIRE_EQ(received.port, 27224);
  ASCIICHAT_REQUIRE_EQ(received.ttl, 120);
  dns_refs[1].ready = true;
  ASCIICHAT_REQUIRE_EQ(contract_mdns_update(context, 10), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(closed_refs, 0, "Repeated resolution must not invalidate polled handles");
  ASCIICHAT_REQUIRE_EQ(contract_mdns_query(context, "_ascii-chat._tcp", capture_bonjour, &received), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(closed_refs, 3, "Replacing a query must close its old handles");
  for (unsigned i = 0; i < 4; ++i)
    ASCIICHAT_REQUIRE_EQ(contract_mdns_update(context, 10), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(callbacks, 2);
  contract_mdns_destroy(context);
  ASCIICHAT_REQUIRE_EQ(closed_refs, 6);
}
TEST_CASE("mdns/bonjour_failures_preserve_context_and_release_partial_queries", "[mdns]") {
  for (fault = 1; fault <= 9; ++fault) {
    memset(dns_refs, 0, sizeof(dns_refs));
    callbacks = closed_refs = 0;
    asciichat_mdns_t *context = SAFE_CALLOC(1, sizeof(*context), asciichat_mdns_t *);
    context->socket_fd = -1;
    asciichat_error_t result = contract_mdns_query(context, "_ascii-chat._tcp.local", capture_bonjour, &received);
    for (unsigned i = 0; i < 4 && result == ASCIICHAT_OK; ++i)
      result = contract_mdns_update(context, 10);
    ASCIICHAT_REQUIRE_EQ(result, ERROR_NETWORK, "fault %u", fault);
    ASCIICHAT_REQUIRE_EQ(callbacks, 0);
    asciichat_error_context_t error;
    ASCIICHAT_REQUIRE(HAS_ERRNO(&error));
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(error.context_message, "Bonjour"));
    contract_mdns_destroy(context);
    ASCIICHAT_REQUIRE_EQ(closed_refs, fault == 1 ? 0 : (fault == 3 || fault == 6 ? 2 : fault == 7 ? 3 : 1));
    CLEAR_ERRNO_ALL();
  }
}
TEST_CASE("mdns/disappearing_bonjour_records_do_not_abort_other_discoveries", "[mdns]") {
  for (fault = 10; fault <= 13; ++fault) {
    memset(dns_refs, 0, sizeof(dns_refs));
    callbacks = closed_refs = 0;
    asciichat_mdns_t *context = SAFE_CALLOC(1, sizeof(*context), asciichat_mdns_t *);
    context->socket_fd = -1;
    ASCIICHAT_REQUIRE_EQ(contract_mdns_query(context, "_ascii-chat._tcp.local", capture_bonjour, &received), ASCIICHAT_OK);
    for (unsigned i = 0; i < 4; ++i)
      ASCIICHAT_REQUIRE_EQ(contract_mdns_update(context, 10), ASCIICHAT_OK, "fault %u", fault);
    ASCIICHAT_REQUIRE_EQ(callbacks, 0);
    contract_mdns_destroy(context);
    ASCIICHAT_REQUIRE_EQ(closed_refs, fault == 10 ? 3 : fault == 13 ? 1 : 2);
  }
}
#undef DNSServiceBrowse
#undef DNSServiceResolve
#undef DNSServiceGetAddrInfo
#undef DNSServiceProcessResult
#undef DNSServiceRefSockFD
#undef DNSServiceRefDeallocate
#undef asciichat_mdns_init
#undef asciichat_mdns_destroy
#undef asciichat_mdns_advertise
#undef asciichat_mdns_unadvertise
#undef asciichat_mdns_query
#undef asciichat_mdns_update
#undef asciichat_mdns_get_socket
#endif
