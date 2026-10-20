#include "connection_internal.h"
#include "crypto.h"
#include "server.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/crypto/discovery_keys.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/ui/notice.h>
#include <ascii-chat/util/endian.h>

/* Security storage and initialization belong to the shared connection owner.
 * This module retains CLI discovery identity verification and rekey actions. */
asciichat_error_t client_crypto_init(client_connection_t *connection) {
  asciichat_error_t result = network_connection_prepare_security(connection->network, false, NULL);
  if (result != ASCIICHAT_OK)
    return result;
  connection->crypto_ctx = network_connection_handshake(connection->network);
  connection->crypto_mode = network_connection_crypto_mode(connection->network);
  result = client_connection_checkpoint("crypto");
  if (result != ASCIICHAT_OK)
    return result;
  // If --require-client-verify is set, perform ACDS session lookup for server identity
  if (GET_OPTION(require_client_verify) && strlen(GET_OPTION(session_string)) > 0) {
    log_debug("--require-client-verify enabled: performing ACDS session lookup for '%s'", GET_OPTION(session_string));

    // Connect to ACDS server (configurable via --acds-server and --acds-port options)
    acds_client_config_t acds_config;
    acds_client_config_init_defaults(&acds_config);
    SAFE_STRNCPY(acds_config.server_address, GET_OPTION(discovery_server), sizeof(acds_config.server_address));
    acds_config.server_port = GET_OPTION(discovery_port);
    acds_config.timeout_ms = 5 * MS_PER_SEC_INT;
    acds_config.identity_key = GET_OPTION(encrypt_key);

    // ACDS key verification (optional in debug builds, only if --discovery-service-key is provided)
    if (strlen(GET_OPTION(discovery_service_key)) > 0) {
      log_debug("Verifying ACDS server key for %s...", acds_config.server_address);
      uint8_t acds_pubkey[32];
      asciichat_error_t verify_result =
          discovery_keys_verify(acds_config.server_address, GET_OPTION(discovery_service_key), acds_pubkey);
      if (verify_result != ASCIICHAT_OK) {
        NOTICE(DANGER, "ACDS VERIFICATION FAILED", "ACDS key verification failed for %s", acds_config.server_address);
        return SET_ERRNO(ERROR_CRYPTO_AUTH, "Cannot verify discovery host identity");
      }
      log_debug("ACDS server key verified successfully");
    }
#ifndef NDEBUG
    else {
      log_debug("Skipping ACDS key verification (debug build, no --discovery-service-key provided)");
    }
#endif

    acds_client_t acds_client;
    asciichat_error_t acds_result = acds_client_connect(&acds_client, &acds_config);
    if (acds_result != ASCIICHAT_OK) {
      log_error("Failed to connect to ACDS server at %s:%d", acds_config.server_address, acds_config.server_port);
      return SET_ERRNO(ERROR_CRYPTO_AUTH, "Cannot verify discovery host identity");
    }

    // Perform SESSION_LOOKUP to get server's identity
    acds_session_lookup_result_t lookup_result;
    acds_result = acds_session_lookup(&acds_client, GET_OPTION(session_string), &lookup_result);
    acds_client_disconnect(&acds_client);

    if (acds_result != ASCIICHAT_OK || !lookup_result.found) {
      log_error("ACDS session lookup failed for '%s': %s", GET_OPTION(session_string),
                lookup_result.found ? "session not found" : "lookup error");
      return SET_ERRNO(ERROR_CRYPTO_AUTH, "Cannot verify discovery host identity");
    }

    // Convert server's Ed25519 public key (32 bytes) to hex string
    char server_key_hex[65]; // 32 bytes * 2 + null terminator
    for (size_t i = 0; i < 32; i++) {
      SAFE_SNPRINTF(&server_key_hex[i * 2], 3, "%02x", lookup_result.host_pubkey[i]);
    }
    server_key_hex[64] = '\0';

    // Set expected server key for verification during handshake
    connection->crypto_ctx->verify_server_key = true;
    SAFE_STRNCPY(connection->crypto_ctx->expected_server_key, server_key_hex,
                 sizeof(connection->crypto_ctx->expected_server_key) - 1);
    log_debug("ACDS session lookup succeeded - server identity will be verified");
    log_debug("Expected server key (from ACDS): %s", server_key_hex);
  }

  return ASCIICHAT_OK;
}

static bool crypto_client_is_ready(client_connection_t *connection) {
  if (!connection->crypto_ctx || connection->crypto_mode == ACIP_CRYPTO_NONE) {
    return false;
  }

  return crypto_handshake_is_ready(connection->crypto_ctx);
}

// =============================================================================
// Session Rekeying Functions
// =============================================================================

/**
 * Check if session rekeying should be triggered
 *
 * @return true if rekey should be initiated, false otherwise
 *
 * @ingroup client_crypto
 */
bool crypto_client_should_rekey(client_connection_t *connection) {
  if (!crypto_client_is_ready(connection) || !connection->crypto_ctx->crypto_ctx.encrypt_data) {
    return false;
  }
  return crypto_handshake_should_rekey(connection->crypto_ctx);
}

/**
 * Initiate session rekeying (client-initiated)
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
asciichat_error_t crypto_client_initiate_rekey(client_connection_t *connection) {
  if (!crypto_client_is_ready(connection)) {
    log_error("Cannot initiate rekey: crypto not initialized or not ready");
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection is not ready for rekeying");
  }

  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport) {
    log_error("Cannot initiate rekey: invalid transport");
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection is not ready for rekeying");
  }

  asciichat_error_t result = crypto_handshake_rekey_request(connection->crypto_ctx, transport);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_REQUEST: %d", result);
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * Process received REKEY_REQUEST packet from server
 *
 * @param packet Packet data
 * @param packet_len Packet length
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
asciichat_error_t crypto_client_process_rekey_request(client_connection_t *connection, const uint8_t *packet,
                                                      size_t packet_len) {
  if (!crypto_client_is_ready(connection)) {
    log_error("Cannot process rekey request: crypto not initialized or not ready");
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection is not ready for rekeying");
  }

  asciichat_error_t result = crypto_handshake_process_rekey_request(connection->crypto_ctx, packet, packet_len);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_REQUEST: %d", result);
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * Send REKEY_RESPONSE packet to server
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
asciichat_error_t crypto_client_send_rekey_response(client_connection_t *connection) {
  if (!crypto_client_is_ready(connection)) {
    log_error("Cannot send rekey response: crypto not initialized or not ready");
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection is not ready for rekeying");
  }

  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport) {
    log_error("Cannot send rekey response: invalid transport");
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection is not ready for rekeying");
  }

  asciichat_error_t result = crypto_handshake_rekey_response(connection->crypto_ctx, transport);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_RESPONSE: %d", result);
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * Process received REKEY_RESPONSE packet from server
 *
 * @param packet Packet data
 * @param packet_len Packet length
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
asciichat_error_t crypto_client_process_rekey_response(client_connection_t *connection, const uint8_t *packet,
                                                       size_t packet_len) {
  if (!crypto_client_is_ready(connection)) {
    log_error("Cannot process rekey response: crypto not initialized or not ready");
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection is not ready for rekeying");
  }

  asciichat_error_t result = crypto_handshake_process_rekey_response(connection->crypto_ctx, packet, packet_len);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_RESPONSE: %d", result);
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * Send REKEY_COMPLETE packet to server and commit to new key
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
asciichat_error_t crypto_client_send_rekey_complete(client_connection_t *connection) {
  if (!crypto_client_is_ready(connection)) {
    log_error("Cannot send rekey complete: crypto not initialized or not ready");
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection is not ready for rekeying");
  }

  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport) {
    log_error("Cannot send rekey complete: invalid transport");
    return SET_ERRNO(ERROR_INVALID_STATE, "Connection is not ready for rekeying");
  }

  asciichat_error_t result = crypto_handshake_rekey_complete(connection->crypto_ctx, transport);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_COMPLETE: %d", result);
    return result;
  }

  return ASCIICHAT_OK;
}
