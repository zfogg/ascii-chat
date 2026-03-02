/**
 * @file log/websocket.c
 * @brief Libwebsockets logging integration
 *
 * Routes libwebsockets log output through our logging system.
 */

#include <ascii-chat/log/websocket.h>
#include <ascii-chat/log/log.h>
#include <libwebsockets.h>

/**
 * @brief Custom logging callback for libwebsockets (server-side)
 */
static void websocket_server_lws_log_callback(int level, const char *line) {
  if (level & LLL_ERR) {
    log_error("[LWS:server] %s", line);
  } else if (level & LLL_WARN) {
    log_warn("[LWS:server] %s", line);
  } else if (level & LLL_NOTICE) {
    log_info("[LWS:server] %s", line);
  } else if (level & LLL_INFO) {
    log_info("[LWS:server] %s", line);
  } else if (level & LLL_DEBUG) {
    log_debug("[LWS:server] %s", line);
  }
}

/**
 * @brief Custom logging callback for libwebsockets (client-side)
 */
static void websocket_client_lws_log_callback(int level, const char *line) {
  if (level & LLL_ERR) {
    log_error("[LWS:client] %s", line);
  } else if (level & LLL_WARN) {
    log_warn("[LWS:client] %s", line);
  } else if (level & LLL_NOTICE) {
    log_info("[LWS:client] %s", line);
  } else if (level & LLL_INFO) {
    log_info("[LWS:client] %s", line);
  } else if (level & LLL_DEBUG) {
    log_debug("[LWS:client] %s", line);
  }
}

void lws_log_init_server(void) {
  // Enable libwebsockets logging with our custom callback (DEBUG disabled to reduce noise)
  lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO, websocket_server_lws_log_callback);
}

void lws_log_init_client(void) {
  // Enable libwebsockets logging with our custom callback (DEBUG disabled to reduce noise)
  lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO, websocket_client_lws_log_callback);
}
