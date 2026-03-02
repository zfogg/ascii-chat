/**
 * @file log/websocket.h
 * @brief Libwebsockets logging integration
 *
 * Routes libwebsockets log output through our logging system.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize libwebsockets logging for server mode
 *
 * Sets up the lws_set_log_level() callback to route server logs
 * through our logging system with [LWS:server] prefix.
 */
void lws_log_init_server(void);

/**
 * @brief Initialize libwebsockets logging for client mode
 *
 * Sets up the lws_set_log_level() callback to route client logs
 * through our logging system with [LWS:client] prefix.
 */
void lws_log_init_client(void);

#ifdef __cplusplus
}
#endif
