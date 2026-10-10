/**
 * @file server_like.c
 * @brief Unified lifecycle for server-like modes (server, discovery-service)
 * @ingroup session
 *
 * Owns TCP server, WebSocket server, mDNS, UPnP, status screen, keyboard,
 * and signal handling. Modes plug in via callbacks.
 */

#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/controller.h>
#include "server_like.h"
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <signal.h>
#include <string.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

static tcp_server_t g_tcp_server;
static bool g_tcp_server_initialized = false;

static websocket_server_t g_websocket_server;
static bool g_websocket_initialized = false;
static asciichat_thread_t g_websocket_thread;
static bool g_websocket_thread_started = false;

static asciichat_mdns_t *g_mdns_ctx = NULL;
static nat_upnp_context_t *g_upnp_ctx = NULL;
static nat_upnp_context_t *g_ws_upnp_ctx = NULL;
static asciichat_thread_t g_upnp_thread;
static bool g_upnp_thread_started = false;
static atomic_t g_upnp_stop = {0};
static atomic_t g_upnp_ready = {0};

// The worker is the sole mapping writer after mode initialization. Shutdown
// joins it before freeing the context or issuing the delete request.
static void *upnp_renewal_thread(void *unused) {
  (void)unused;
  while (!atomic_load_bool(&g_upnp_stop)) {
    if (atomic_load_bool(&g_upnp_ready)) {
      nat_upnp_context_t *mappings[] = {g_upnp_ctx, g_ws_upnp_ctx};
      for (size_t i = 0; i < sizeof(mappings) / sizeof(mappings[0]); i++) {
        if (mappings[i] && time_get_ns() >= mappings[i]->refresh_at_ns) {
          if (nat_upnp_refresh(mappings[i]) != ASCIICHAT_OK) {
            LOG_ERRNO_IF_SET("Router mapping renewal failed");
          }
        }
      }
    }
    time_sleep_ns(100 * NS_PER_MS_INT);
  }
  return NULL;
}

static asciichat_thread_t g_status_screen_thread;
static bool g_status_screen_thread_started = false;

/** Status screen callback and user data (immutable after thread start, cleared at shutdown). */
static session_server_like_status_fn g_status_fn = NULL;
static void *g_status_user_data = NULL;

/** Stored config pointer (valid during session_server_like_run scope). */
static const session_server_like_config_t *g_config = NULL;

/* ============================================================================
 * Signal Handler
 * ============================================================================ */

/** Persistent shutdown request flag (set by interrupt handler during early init). */
static atomic_t g_shutdown_requested = {0};

static session_server_like_interrupt_fn g_interrupt_fn = NULL;

static void server_like_signal_handler(int sig) {
  atomic_store_u64(&g_shutdown_requested, 1);
  if (g_interrupt_fn) {
    g_interrupt_fn(sig);
  }
}

/* ============================================================================
 * Keyboard Queue (lock-free SPSC ring buffer)
 * ============================================================================ */

/* ============================================================================
 * Status Screen Thread
 * ============================================================================ */

/** Atomic flag checked by the status screen loop. Set by signal handler. */
static atomic_t g_status_should_exit = {0};

static void *status_screen_thread_func(void *arg) {
  (void)arg;

  uint32_t fps = GET_OPTION(fps);
  if (fps == 0) {
    fps = 60;
  }
  uint64_t frame_interval_us = US_PER_SEC_INT / fps;

  log_debug("Status screen thread started (target %u FPS)", fps);

  int output_fd = platform_isatty(STDOUT_FILENO) ? STDOUT_FILENO : STDERR_FILENO;
  bool keyboard_enabled = platform_isatty(STDIN_FILENO) && platform_isatty(output_fd);

  // Stderr is the status output when stdout is redirected; retain it in that
  // case. The presentation controller suppresses competing terminal log writes.
  platform_stderr_redirect_handle_t stderr_redirect = {.original_fd = -1, .devnull_fd = -1};
  if (output_fd != STDERR_FILENO) {
    stderr_redirect = platform_stderr_redirect_to_null();
  }

  bool skip_next_slash = false;
  bool grep_was_just_cancelled = false;

  while (!atomic_load_bool(&g_status_should_exit)) {
    uint64_t frame_start = platform_get_monotonic_time_us();

    // Check if SIGINT handler cancelled grep mode
    if (log_search_check_signal_cancel()) {
      log_search_exit_mode(false);
      grep_was_just_cancelled = true;
    }

    // Process keyboard input
    if (keyboard_enabled) {
      grep_was_just_cancelled = false;

      keyboard_key_t key = ui_input_read_key(UI_SCREEN_STATUS);
      while (key != KEY_NONE && !grep_was_just_cancelled) {
        if (skip_next_slash && key == '/') {
          skip_next_slash = false;
          key = ui_input_read_key(UI_SCREEN_STATUS);
          continue;
        }
        skip_next_slash = false;

        if (log_search_should_handle(key)) {
          bool was_not_in_grep = !log_search_is_entering();
          bool was_in_grep = log_search_is_entering();

          log_search_handle_key(key);

          if (was_not_in_grep && log_search_is_entering() && key == '/') {
            skip_next_slash = true;
          }

          if (was_in_grep && !log_search_is_entering()) {
            grep_was_just_cancelled = true;
            break;
          }
        }

        key = ui_input_read_key(UI_SCREEN_STATUS);
      }
    }

    // Call mode's status callback to populate status data
    // Use direct static pointers instead of g_config to avoid stale reference during shutdown
    if (g_status_fn) {
      ui_status_t status;
      memset(&status, 0, sizeof(status));
      g_status_fn(g_status_user_data, &status);
      ui_status_display_interactive(&status);
    }

    // Maintain frame rate
    uint64_t frame_end = platform_get_monotonic_time_us();
    uint64_t frame_time = frame_end - frame_start;
    if (frame_time < frame_interval_us) {
      platform_sleep_us(frame_interval_us - frame_time);
    }
  }

  ui_controller_remove(UI_SCREEN_STATUS);

  platform_stderr_restore(stderr_redirect);

  log_debug("Status screen thread exiting");
  return NULL;
}

/* ============================================================================
 * WebSocket Server Thread
 * ============================================================================ */

static void *websocket_server_thread_wrapper(void *arg) {
  websocket_server_t *server = (websocket_server_t *)arg;
  asciichat_error_t result = websocket_server_run(server);
  if (result != ASCIICHAT_OK) {
    log_error("WebSocket server thread exited with error");
  }
  return NULL;
}

/* ============================================================================
 * Accessors
 * ============================================================================ */

tcp_server_t *session_server_like_get_tcp_server(void) {
  return g_tcp_server_initialized ? &g_tcp_server : NULL;
}

asciichat_mdns_t *session_server_like_get_mdns_ctx(void) {
  return g_mdns_ctx;
}

nat_upnp_context_t *session_server_like_get_upnp_ctx(void) {
  return g_upnp_ctx;
}

websocket_server_t *session_server_like_get_websocket_server(void) {
  return g_websocket_thread_started ? &g_websocket_server : NULL;
}

/* ============================================================================
 * Helpers
 * ============================================================================ */

asciichat_error_t session_server_like_mdns_advertise(const char *name, const char *type, uint16_t port,
                                                     const char **txt, size_t txt_count) {
  if (!g_mdns_ctx) {
    return SET_ERRNO(ERROR_INVALID_STATE, "mDNS context not initialized");
  }

  char hostname[256] = {0};
  gethostname(hostname, sizeof(hostname) - 1);

  asciichat_mdns_service_t service = {
      .name = name,
      .type = type,
      .host = hostname,
      .port = port,
      .txt_records = txt,
      .txt_count = txt_count,
  };

  asciichat_error_t result = asciichat_mdns_advertise(g_mdns_ctx, &service);
  if (result != ASCIICHAT_OK) {
    LOG_ERRNO_IF_SET("Failed to advertise mDNS service");
    log_warn("mDNS advertising failed - LAN discovery disabled");
  } else {
    log_info("mDNS: Service advertised as '%s' type '%s' (port=%d)", name, type, port);
  }

  return result;
}

/* ============================================================================
 * Accessors (shutdown state and network resources)
 * ============================================================================ */

void session_server_like_request_shutdown(void) {
  server_like_signal_handler(SIGINT);
}

bool session_server_like_shutdown_requested(void) {
  return (bool)atomic_load_u64(&g_shutdown_requested);
}

/* ============================================================================
 * Shared Handshake
 * ============================================================================ */

static asciichat_error_t session_server_like_handshake_impl(crypto_handshake_context_t *ctx,
                                                            acip_transport_t *transport) {
  if (!ctx || !transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "handshake context or transport is NULL");
  }

  asciichat_error_t result;

  // Step 0: Receive PROTOCOL_VERSION from client
  {
    packet_type_t pkt_type;
    void *pkt_data = NULL;
    size_t pkt_len = 0;
    void *alloc_buffer = NULL;

    result = packet_receive_via_transport(transport, &pkt_type, &pkt_data, &pkt_len, &alloc_buffer);
    if (result != ASCIICHAT_OK) {
      log_warn("Failed to receive PROTOCOL_VERSION from client");
      return result;
    }

    if (pkt_type != PACKET_TYPE_PROTOCOL_VERSION) {
      log_warn("Expected PROTOCOL_VERSION, got packet type %u", pkt_type);
      buffer_pool_free(NULL, alloc_buffer, 0);
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected PROTOCOL_VERSION packet");
    }

    buffer_pool_free(NULL, alloc_buffer, 0);
    log_debug("Received PROTOCOL_VERSION from client");
  }

  // Step 1: Send crypto parameters
  result = crypto_handshake_server_send_parameters(ctx, transport);
  if (result != ASCIICHAT_OK) {
    log_warn("Crypto parameters send failed");
    return result;
  }

  // Step 2: Key exchange (send server key)
  result = crypto_handshake_server_start(ctx, transport);
  if (result != ASCIICHAT_OK) {
    log_warn("Crypto handshake start failed");
    return result;
  }

  // Step 3: Receive client key exchange response, send auth challenge
  {
    packet_type_t pkt_type;
    void *pkt_data = NULL;
    size_t pkt_len = 0;
    void *alloc_buffer = NULL;

    result = packet_receive_via_transport(transport, &pkt_type, &pkt_data, &pkt_len, &alloc_buffer);
    if (result != ASCIICHAT_OK) {
      log_warn("Failed to receive KEY_EXCHANGE_RESPONSE");
      return result;
    }

    result = crypto_handshake_server_auth_challenge(ctx, transport, pkt_type, pkt_data, pkt_len);
    buffer_pool_free(NULL, alloc_buffer, 0);

    if (result != ASCIICHAT_OK) {
      log_warn("Crypto auth challenge failed");
      return result;
    }
  }

  // Step 4: Complete handshake (skip if already complete from auth challenge)
  if (ctx->state != CRYPTO_HANDSHAKE_READY) {
    packet_type_t pkt_type;
    void *pkt_data = NULL;
    size_t pkt_len = 0;
    void *alloc_buffer = NULL;

    result = packet_receive_via_transport(transport, &pkt_type, &pkt_data, &pkt_len, &alloc_buffer);
    if (result != ASCIICHAT_OK) {
      log_warn("Failed to receive AUTH_RESPONSE");
      return result;
    }

    result = crypto_handshake_server_complete(ctx, transport, pkt_type, pkt_data, pkt_len);
    buffer_pool_free(NULL, alloc_buffer, 0);

    if (result != ASCIICHAT_OK) {
      log_warn("Crypto handshake completion failed");
      return result;
    }
  }

  log_info("Crypto handshake complete");
  return ASCIICHAT_OK;
}

asciichat_error_t session_server_like_handshake(crypto_handshake_context_t *ctx, acip_transport_t *transport) {
  if (!ctx || !transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "handshake context or transport is NULL");
  }

  // Crypto key generation and signing can exceed the normal one-second packet
  // polling interval on slower or busy peers. Keep the longer deadline scoped
  // to the handshake so idle-session polling remains responsive.
  const uint64_t previous_receive_timeout_ns = transport->receive_timeout_ns;
  transport->receive_timeout_ns = 30ULL * NS_PER_SEC_INT;
  asciichat_error_t result = session_server_like_handshake_impl(ctx, transport);
  transport->receive_timeout_ns = previous_receive_timeout_ns;
  return result;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

asciichat_error_t session_server_like_run(const session_server_like_config_t *config) {
  if (!config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "config is NULL");
  }
  if (!config->init_fn) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "init_fn is NULL");
  }
  if (!config->interrupt_fn) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "interrupt_fn is NULL");
  }
  if (!config->tcp_handler) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "tcp_handler is NULL");
  }

  g_config = config;
  asciichat_error_t result = ASCIICHAT_OK;

  /* === 1. Keepawake === */

  bool keepawake_enabled = GET_OPTION(enable_keepawake);
  bool keepawake_disabled = GET_OPTION(disable_keepawake);
  if (keepawake_enabled && keepawake_disabled) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Cannot specify both --keepawake and --no-keepawake");
  }
  if (keepawake_enabled) {
    platform_enable_keepawake();
  }

  /* === 2. File descriptor limit === */

  if (config->raise_fd_limit) {
    platform_raise_fd_limit(config->fd_limit_target);
  }

  /* === 3. Signal handlers === */

  atomic_store_u64(&g_shutdown_requested, 0);
  g_interrupt_fn = config->interrupt_fn;
  platform_signal(SIGINT, server_like_signal_handler);
  platform_signal(SIGTERM, server_like_signal_handler);
#ifndef _WIN32
  platform_signal(SIGPIPE, SIG_IGN);
#endif

  /* === 4. TCP server init === */

  int port = GET_OPTION(port);
  const char *address = GET_OPTION(address);
  const char *address6 = GET_OPTION(address6);

  bool ipv4_has_value = (address && strlen(address) > 0);
  bool ipv6_has_value = (address6 && strlen(address6) > 0);

  tcp_server_config_t tcp_config = {
      .port = port,
      .ipv4_address = ipv4_has_value ? address : NULL,
      .ipv6_address = ipv6_has_value ? address6 : NULL,
      .bind_ipv4 = ipv4_has_value || !ipv6_has_value,
      .bind_ipv6 = ipv6_has_value || !ipv4_has_value,
      .accept_timeout_sec = 1,
      .client_handler = config->tcp_handler,
      .user_data = config->tcp_user_data,
      .status_update_fn = NULL,
      .status_update_data = NULL,
  };

  result = tcp_server_init(&g_tcp_server, &tcp_config);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to initialize TCP server on port %d", port);
    goto cleanup;
  }
  g_tcp_server_initialized = true;
  log_info("TCP server initialized on port %d", port);

  /* === 5. UPnP port mapping === */

  bool upnp_requested = config->upnp.enabled && GET_OPTION(enable_upnp);
  bool ipv4_loopback = ipv4_has_value && is_localhost_ipv4(address);
  bool ipv4_listener = tcp_config.bind_ipv4 && !ipv4_loopback;
  if (upnp_requested && ipv4_listener) {
    log_info("UPnP status: discovering (TCP port %d)", port);
    asciichat_error_t upnp_result = nat_upnp_open(port, config->upnp.description, &g_upnp_ctx);
    if (upnp_result == ASCIICHAT_OK && !nat_upnp_matches_bind_address(g_upnp_ctx, tcp_config.ipv4_address)) {
      log_warn("NAT: gateway mapping does not target the bound IPv4 listener; removing it");
      nat_upnp_close(&g_upnp_ctx);
      upnp_result = ERROR_NETWORK;
    }
    if (upnp_result == ASCIICHAT_OK && nat_upnp_is_active(g_upnp_ctx)) {
      char public_addr[22];
      if (nat_upnp_get_address(g_upnp_ctx, public_addr, sizeof(public_addr)) == ASCIICHAT_OK) {
        log_info("UPnP status: mapped at %s (external reachability unverified)", public_addr);
      }
    } else {
      log_warn("UPnP status: unavailable. Listener remains enabled; for Internet access, "
               "check router support or manually forward TCP port %d. External reachability is unverified.",
               port);
    }
  } else if (upnp_requested) {
    log_info("UPnP status: disabled (requires a non-loopback IPv4 listener)");
  } else {
    log_info("UPnP status: disabled");
    if (ipv4_listener && (!ipv4_has_value || strcmp(address, "0.0.0.0") == 0 || is_lan_ipv4(address))) {
      log_info("For connections from outside your LAN, try --port-forwarding or forward TCP port %d on your router.",
               port);
    }
  }

  /* === 6. mDNS === */

  if (config->mdns.enabled) {
    g_mdns_ctx = asciichat_mdns_init();
    if (!g_mdns_ctx) {
      LOG_ERRNO_IF_SET("Failed to initialize mDNS (non-fatal, LAN discovery disabled)");
      log_warn("mDNS disabled - LAN discovery will not be available");
    } else if (config->mdns.service_name) {
      // Immediate advertisement if service_name is provided
      session_server_like_mdns_advertise(config->mdns.service_name, config->mdns.service_type, port, NULL, 0);
    }
  }

  /* === 7. WebSocket server construction (thread deferred until after mode init) === */

  if (config->websocket.enabled) {
    websocket_server_config_t ws_config = {
        .port = GET_OPTION(websocket_port),
        .client_handler = config->websocket.handler,
        .user_data = config->websocket.user_data,
        .tls_cert_path = GET_OPTION(websocket_tls_cert),
        .tls_key_path = GET_OPTION(websocket_tls_key),
    };

    memset(&g_websocket_server, 0, sizeof(g_websocket_server));
    asciichat_error_t ws_result = websocket_server_init(&g_websocket_server, &ws_config);
    if (ws_result != ASCIICHAT_OK) {
      log_warn("Failed to initialize WebSocket server - browser clients will not be supported");
    } else {
      g_websocket_initialized = true;
      log_info("WebSocket server initialized on port %d (event loop deferred until after mode init)",
               GET_OPTION(websocket_port));
    }
  }

  /* === 8. Mode-specific init === */

  result = config->init_fn(config->init_user_data);
  if (result != ASCIICHAT_OK) {
    log_error("Mode initialization failed");
    goto cleanup;
  }

  /* === 8b. WebSocket event loop thread start (after mode init succeeds) === */

  if (g_websocket_initialized && !atomic_load_u64(&g_shutdown_requested)) {
    if (asciichat_thread_create(&g_websocket_thread, "websocket_event_loop", websocket_server_thread_wrapper,
                                &g_websocket_server) == 0) {
      g_websocket_thread_started = true;
      log_info("WebSocket event loop thread started");
    } else {
      log_error("Failed to start WebSocket event loop thread");
    }
  }

  if (upnp_requested && g_websocket_thread_started) {
    // WebSocket binds independently of the raw TCP listener's address.
    if (nat_upnp_open((uint16_t)GET_OPTION(websocket_port), "ascii-chat WebSocket", &g_ws_upnp_ctx) != ASCIICHAT_OK) {
      log_warn("WebSocket mapping unavailable; manually forward TCP port %d if needed", GET_OPTION(websocket_port));
    }
  }

  if (g_upnp_ctx || g_ws_upnp_ctx) {
    atomic_store_bool(&g_upnp_stop, false);
    atomic_store_bool(&g_upnp_ready, false);
    if (asciichat_thread_create(&g_upnp_thread, "upnp_renewal", upnp_renewal_thread, NULL) == 0) {
      g_upnp_thread_started = true;
    } else {
      log_warn("NAT: could not start lease renewal; removing the temporary mapping");
      nat_upnp_close(&g_upnp_ctx);
      nat_upnp_close(&g_ws_upnp_ctx);
    }
  }

  // Keep bounded log capture available even when only the stats overlay is used.
  ui_status_log_init();
  /* === 9. Status screen === */

  if (config->status_fn) {
    bool status_opt = GET_OPTION(status_screen);
    bool status_explicit = GET_OPTION(status_screen_explicitly_set);
    if ((status_opt && terminal_is_interactive()) || (status_explicit && status_opt)) {
      atomic_store_bool(&g_status_should_exit, false);

      // Store callback and user data for status thread (avoids reliance on g_config lifetime)
      g_status_fn = config->status_fn;
      g_status_user_data = config->status_user_data;

      if (asciichat_thread_create(&g_status_screen_thread, "status_screen", status_screen_thread_func, NULL) == 0) {
        g_status_screen_thread_started = true;
        log_info("Status screen thread started");
      } else {
        log_error("Failed to start status screen thread");
        ui_status_log_destroy();
        g_status_fn = NULL;
        g_status_user_data = NULL;
      }
    }
  }

  atomic_store_bool(&g_upnp_ready, true);

  /* === 10. TCP accept loop (blocks) === */

  log_info("Server accepting connections on port %d", port);
  result = tcp_server_run(&g_tcp_server);
  if (result != ASCIICHAT_OK) {
    log_error("TCP server exited with error");
  }

  /* === CLEANUP === */

cleanup:
  g_config = NULL;
  if (g_upnp_thread_started) {
    atomic_store_bool(&g_upnp_stop, true);
    asciichat_thread_join(&g_upnp_thread, NULL);
    g_upnp_thread_started = false;
  }

  // Remove the mapping before mode teardown, which can wait on other services.
  if (g_upnp_ctx) {
    nat_upnp_close(&g_upnp_ctx);
    log_debug("UPnP port mapping closed");
  }

  nat_upnp_close(&g_ws_upnp_ctx);

  /* 11. Stop status screen */
  if (g_status_screen_thread_started) {
    atomic_store_bool(&g_status_should_exit, true);
    int join_result = asciichat_thread_join_timeout(&g_status_screen_thread, NULL, 2000 * NS_PER_MS_INT);
    if (join_result != 0) {
      log_warn("Status screen thread did not exit within 2s");
    }
    g_status_screen_thread_started = false;
    g_status_fn = NULL;
    g_status_user_data = NULL;
  }
  ui_status_log_destroy();

  /* 12. Stop WebSocket server (only if thread was started, i.e., init succeeded) */
  if (g_websocket_thread_started) {
    atomic_store_bool(&g_websocket_server.running, false);
    websocket_server_cancel_service(&g_websocket_server);
    int ws_join = asciichat_thread_join_timeout(&g_websocket_thread, NULL, 500 * NS_PER_MS_INT);
    if (ws_join != 0) {
      log_warn("WebSocket event loop thread did not exit within 500ms");
    }
    g_websocket_thread_started = false;
  }

  /* 13. Mode-specific cleanup (only call if config provided) */
  if (config && config->cleanup_fn) {
    config->cleanup_fn(config->cleanup_user_data);
  }

  /* 14. Destroy WebSocket server (only if successfully initialized) */
  if (g_websocket_initialized) {
    websocket_server_destroy(&g_websocket_server);
    g_websocket_initialized = false;
    log_debug("WebSocket server destroyed");
  }

  /* 15. Destroy TCP server */
  if (g_tcp_server_initialized) {
    tcp_server_destroy(&g_tcp_server);
    g_tcp_server_initialized = false;
    log_debug("TCP server destroyed");
  }

  /* 17. mDNS cleanup */
  if (g_mdns_ctx) {
    asciichat_mdns_destroy(g_mdns_ctx);
    g_mdns_ctx = NULL;
    log_debug("mDNS context destroyed");
  }

  /* 18. Keepawake */
  if (keepawake_enabled) {
    platform_disable_keepawake();
  }

  return result;
}
