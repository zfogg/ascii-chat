#pragma once
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/negotiation.h>

typedef asciichat_error_t (*network_connection_prepare_fn)(network_connection_t *, const connection_endpoint_t *,
                                                           const char *, void *);

typedef struct {
  const char *name, *address;
  uint16_t default_port;
  const network_connect_config_t *connect;
  const char *password;
  network_protocol_t protocol;
  bool authenticate;
  network_connection_prepare_fn prepare;
  void *context;
/**
 */
} network_establish_config_t;

/**
 * @brief This is the handoff point for network establish config. Callers fill `name`, `address`, `default_port`, `connect` before passing it on; consumers read the values during the operation and return ownership only where the member contract explicitly says they do.
 */
typedef struct {
  connection_endpoint_t endpoint;
  uint8_t password_failure;
/**
 * @brief The useful part of network establish result is in `endpoint`, `password_failure`: together they describe what is pending, who owns it, and how much data is valid. Keep the object with its parent operation and use the parent cleanup path.
 */
} network_establish_result_t;

/* Open and negotiate one noninteractive attempt. prepare configures connection-owned
 * security and caller-specific identity verification. The caller retains the owner
 * on failure and must stop all borrowers before destroying it. A password challenge
 * is reported to the caller; this function never prompts, retries or disposes it. */
asciichat_error_t network_connection_establish(const network_establish_config_t *config, network_connection_t **owner,
                                               network_establish_result_t *attempt);
