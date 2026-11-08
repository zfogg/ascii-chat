#include <ascii-chat/crypto/sizes.h>
#include "connection_internal.h"
#include "crypto.h"
#include "server.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/crypto/discovery_keys.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/tui/notice.h>
#include <ascii-chat/util/endian.h>

/* Security storage and initialization belong to the shared connection owner.
 * This module retains CLI discovery identity verification. */
asciichat_error_t client_crypto_init(client_connection_t *connection, const char *password) {
  asciichat_error_t result = network_connection_prepare_security(connection->network, false, password);
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
      uint8_t acds_pubkey[ED25519_PUBLIC_KEY_SIZE];
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
    char server_key_hex[HEX_STRING_SIZE_32]; // 32 bytes * 2 + null terminator
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
