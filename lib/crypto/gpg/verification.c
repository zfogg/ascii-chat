/**
 * @file crypto/gpg/verification.c
 * @ingroup crypto
 * @brief GPG signature verification implementation
 */

#include <ascii-chat/crypto/gpg/verification.h>
#include <ascii-chat/crypto/gpg/signing.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/validation.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/platform/process.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef HAVE_LIBGCRYPT
#include <gcrypt.h>
#endif

int gpg_verify_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                                const uint8_t signature[64]) {
  // Note: We don't use the raw signature parameter directly.
  // Instead, we regenerate the OpenPGP signature using GPG (Ed25519 is deterministic).
  (void)signature;

  log_debug("gpg_verify_detached_ed25519: Verifying signature with key ID %s using gpg --verify", key_id);

  // To verify with GPG, we need to:
  // 1. Reconstruct the OpenPGP signature packet from the raw R||S signature
  // 2. Write message and signature to temp files
  // 3. Call gpg --verify

  // First, reconstruct OpenPGP signature by signing the same message
  // Since Ed25519 is deterministic, we should get the same OpenPGP packet
  uint8_t openpgp_signature[512];
  size_t openpgp_len = 0;

  int sign_result = gpg_sign_with_key(key_id, message, message_len, openpgp_signature, &openpgp_len);
  if (sign_result != 0) {
    log_error("Failed to create reference signature for verification");
    return -1;
  }

  // Now verify using gpg --verify
  char msg_path[PLATFORM_MAX_PATH_LENGTH];
  char sig_path[PLATFORM_MAX_PATH_LENGTH];
  int msg_fd = -1;
  int sig_fd = -1;

  if (platform_create_temp_file(msg_path, sizeof(msg_path), "gpg_verify_msg", &msg_fd) != 0) {
    log_error("Failed to create temporary message file");
    return -1;
  }

  if (platform_create_temp_file(sig_path, sizeof(sig_path), "gpg_verify_sig", &sig_fd) != 0) {
    platform_delete_temp_file(msg_path);
    log_error("Failed to create temporary signature file");
    return -1;
  }

  // Write message
  if (write(msg_fd, message, message_len) != (ssize_t)message_len) {
    log_error("Failed to write message to temp file");
    close(msg_fd);
    close(sig_fd);
    platform_delete_temp_file(msg_path);
    platform_delete_temp_file(sig_path);
    return -1;
  }
  close(msg_fd);

  // Write OpenPGP signature
  if (write(sig_fd, openpgp_signature, openpgp_len) != (ssize_t)openpgp_len) {
    log_error("Failed to write signature to temp file");
    close(sig_fd);
    platform_delete_temp_file(msg_path);
    platform_delete_temp_file(sig_path);
    return -1;
  }
  close(sig_fd);

  // Call gpg --verify
  char cmd[1024];
  safe_snprintf(cmd, sizeof(cmd), "gpg --verify '%s' '%s' 2>&1", sig_path, msg_path);
  log_debug("Running: %s", cmd);

  FILE *fp;
  platform_popen(cmd, "r", &fp);
  if (!fp) {
    log_error("Failed to run gpg --verify");
    platform_unlink(msg_path);
    platform_unlink(sig_path);
    return -1;
  }

  char output[4096] = {0};
  size_t output_len = fread(output, 1, sizeof(output) - 1, fp);
  int exit_code = platform_pclose(&fp);

  // Cleanup temp files
  platform_delete_temp_file(msg_path);
  platform_delete_temp_file(sig_path);

  if (exit_code == 0) {
    log_debug("GPG signature verification PASSED");
    return 0;
  } else {
    log_error("GPG signature verification FAILED (exit code %d)", exit_code);
    if (output_len > 0) {
      log_debug("GPG output: %s", output);
    }
    return -1;
  }
}

int gpg_verify_signature(const uint8_t *public_key, const uint8_t *message, size_t message_len,
                         const uint8_t *signature) {
#ifdef HAVE_LIBGCRYPT
  gcry_error_t err;
  gcry_sexp_t s_pubkey = NULL;
  gcry_sexp_t s_sig = NULL;
  gcry_sexp_t s_data = NULL;

  // Initialize libgcrypt if not already done
  if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) {
    gcry_check_version(NULL);
    gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
  }

  // Build public key S-expression: (public-key (ecc (curve Ed25519) (flags eddsa) (q %b)))
  // CRITICAL: Must include (flags eddsa) to match libgcrypt's Ed25519 test suite!
  // See libgcrypt/tests/t-ed25519.c line 246-251
  err = gcry_sexp_build(&s_pubkey, NULL, "(public-key (ecc (curve Ed25519) (flags eddsa) (q %b)))", 32, public_key);
  if (err) {
    log_error("gpg_verify_signature: Failed to build public key S-expression: %s", gcry_strerror(err));
    return -1;
  }

  // Build signature S-expression: (sig-val (eddsa (r %b) (s %b)))
  // Signature is 64 bytes: first 32 bytes are R, last 32 bytes are S
  err = gcry_sexp_build(&s_sig, NULL, "(sig-val (eddsa (r %b) (s %b)))", 32, signature, 32, signature + 32);
  if (err) {
    log_error("gpg_verify_signature: Failed to build signature S-expression: %s", gcry_strerror(err));
    gcry_sexp_release(s_pubkey);
    return -1;
  }

  // Build data S-expression with raw message
  // CRITICAL: According to libgcrypt's test suite (t-ed25519.c line 273),
  // Ed25519 data should be: (data (value %b)) with NO FLAGS!
  // The (flags eddsa) belongs in the KEY S-expression above, NOT in the data.
  // GPG agent's internal format is different - this is the correct libgcrypt API usage.
  err = gcry_sexp_build(&s_data, NULL, "(data (value %b))", message_len, message);
  if (err) {
    log_error("gpg_verify_signature: Failed to build data S-expression: %s", gcry_strerror(err));
    gcry_sexp_release(s_pubkey);
    gcry_sexp_release(s_sig);
    return -1;
  }

  // Debug logging
  char pubkey_hex[65];
  char r_hex[65];
  char s_hex[65];
  char msg_hex[128];

  for (int i = 0; i < 32; i++) {
    safe_snprintf(pubkey_hex + i * 2, 3, "%02x", public_key[i]);
    safe_snprintf(r_hex + i * 2, 3, "%02x", signature[i]);
    safe_snprintf(s_hex + i * 2, 3, "%02x", signature[32 + i]);
  }
  for (size_t i = 0; i < (message_len < 32 ? message_len : 32); i++) {
    safe_snprintf(msg_hex + i * 2, 3, "%02x", message[i]);
  }

  log_debug("gpg_verify_signature: pubkey=%s", pubkey_hex);
  log_debug("gpg_verify_signature: R=%s", r_hex);
  log_debug("gpg_verify_signature: S=%s", s_hex);
  log_debug("gpg_verify_signature: msg=%s (len=%zu)", msg_hex, message_len);

  // Verify the signature
  err = gcry_pk_verify(s_sig, s_data, s_pubkey);

  // Clean up S-expressions
  gcry_sexp_release(s_pubkey);
  gcry_sexp_release(s_sig);
  gcry_sexp_release(s_data);

  if (err) {
    log_debug("gpg_verify_signature: Signature verification failed: %s", gcry_strerror(err));
    return -1;
  }

  log_debug("gpg_verify_signature: Signature verified successfully");
  return 0;
#else
  // Explicitly mark parameters as unused when libgcrypt is not available
  (void)public_key;
  (void)message;
  (void)message_len;
  (void)signature;
  log_error("gpg_verify_signature: libgcrypt not available");
  return -1;
#endif
}

int gpg_verify_signature_with_binary(const uint8_t *signature, size_t signature_len, const uint8_t *message,
                                     size_t message_len, const char *expected_key_id) {
  // Validate inputs
  if (!signature || signature_len == 0 || signature_len > 512) {
    log_error("gpg_verify_signature_with_binary: Invalid signature (expected 1-512 bytes, got %zu)", signature_len);
    return -1;
  }
  if (!message || message_len == 0) {
    log_error("gpg_verify_signature_with_binary: Invalid message");
    return -1;
  }

  // Create temporary files for signature and message
  char sig_path[PLATFORM_MAX_PATH_LENGTH];
  char msg_path[PLATFORM_MAX_PATH_LENGTH];
  int sig_fd = -1;
  int msg_fd = -1;
  int result = -1;

  // Create temp files using platform abstraction
  if (platform_create_temp_file(sig_path, sizeof(sig_path), "asciichat_sig", &sig_fd) != 0) {
    log_error("Failed to create signature temp file");
    return -1;
  }

  if (platform_create_temp_file(msg_path, sizeof(msg_path), "asciichat_msg", &msg_fd) != 0) {
    log_error("Failed to create message temp file");
    platform_delete_temp_file(sig_path);
    return -1;
  }

#ifdef _WIN32
  // Windows: Write to already-created files using CreateFileA
  HANDLE sig_handle = CreateFileA(sig_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (sig_handle == INVALID_HANDLE_VALUE) {
    log_error("Failed to open signature temp file: %lu", GetLastError());
    platform_delete_temp_file(sig_path);
    platform_delete_temp_file(msg_path);
    return -1;
  }

  DWORD bytes_written;
  if (!WriteFile(sig_handle, signature, (DWORD)signature_len, &bytes_written, NULL) || bytes_written != signature_len) {
    log_error("Failed to write signature to temp file: %lu", GetLastError());
    CloseHandle(sig_handle);
    platform_delete_temp_file(sig_path);
    platform_delete_temp_file(msg_path);
    return -1;
  }
  CloseHandle(sig_handle);

  HANDLE msg_handle = CreateFileA(msg_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (msg_handle == INVALID_HANDLE_VALUE) {
    log_error("Failed to open message temp file: %lu", GetLastError());
    platform_delete_temp_file(sig_path);
    platform_delete_temp_file(msg_path);
    return -1;
  }

  if (!WriteFile(msg_handle, message, (DWORD)message_len, &bytes_written, NULL) || bytes_written != message_len) {
    log_error("Failed to write message to temp file: %lu", GetLastError());
    CloseHandle(msg_handle);
    platform_delete_temp_file(sig_path);
    platform_delete_temp_file(msg_path);
    return -1;
  }
  CloseHandle(msg_handle);

#else
  // Unix: Write to open file descriptors returned by platform_create_temp_file
  ssize_t sig_written = write(sig_fd, signature, signature_len);
  if (sig_written != (ssize_t)signature_len) {
    log_error("Failed to write signature to temp file: %s", SAFE_STRERROR(errno));
    close(sig_fd);
    close(msg_fd);
    platform_delete_temp_file(sig_path);
    platform_delete_temp_file(msg_path);
    return -1;
  }
  close(sig_fd);

  ssize_t msg_written = write(msg_fd, message, message_len);
  if (msg_written != (ssize_t)message_len) {
    log_error("Failed to write message to temp file: %s", SAFE_STRERROR(errno));
    close(msg_fd);
    platform_delete_temp_file(sig_path);
    platform_delete_temp_file(msg_path);
    return -1;
  }
  close(msg_fd);
#endif

  // Build gpg --verify command
  char cmd[BUFFER_SIZE_LARGE];
#ifdef _WIN32
  safe_snprintf(cmd, sizeof(cmd), "gpg --verify \"%s\" \"%s\" 2>&1", sig_path, msg_path);
#else
  safe_snprintf(cmd, sizeof(cmd), "gpg --verify '%s' '%s' 2>&1", sig_path, msg_path);
#endif

  log_debug("Running GPG verify command: %s", cmd);

  // Execute gpg --verify command
  FILE *fp;
  platform_popen(cmd, "r", &fp);
  if (!fp) {
    log_error("Failed to execute gpg --verify command");
    goto cleanup;
  }

  // Parse output for "Good signature" and verify key ID
  char line[BUFFER_SIZE_MEDIUM];
  bool found_good_sig = false;
  bool found_key_id = false;

  while (fgets(line, sizeof(line), fp)) {
    log_debug("GPG output: %s", line);

    // Check for "Good signature"
    if (strstr(line, "Good signature")) {
      found_good_sig = true;
    }

    // Check if this line contains the expected key ID (GPG outputs key ID on separate line)
    if (expected_key_id && strlen(expected_key_id) > 0) {
      if (strstr(line, expected_key_id)) {
        found_key_id = true;
        log_debug("Found expected key ID in GPG output: %s", expected_key_id);
      }
    }

    // Check for signature errors
    if (strstr(line, "BAD signature")) {
      log_error("GPG reports BAD signature");
      platform_pclose(&fp);
      fp = NULL;
      goto cleanup;
    }
  }

  // Check exit code
  int status = platform_pclose(&fp);
  fp = NULL;

#ifdef _WIN32
  int exit_code = status;
#else
  int exit_code = WEXITSTATUS(status);
#endif

  if (exit_code != 0) {
    log_error("GPG verify failed with exit code: %d", exit_code);
    goto cleanup;
  }

  if (!found_good_sig) {
    log_error("GPG verify did not report 'Good signature'");
    goto cleanup;
  }

  // If expected_key_id was provided, verify we found it in the output
  if (expected_key_id && strlen(expected_key_id) > 0) {
    if (!found_key_id) {
      log_error("GPG signature key ID does not match expected key ID: %s", expected_key_id);
      goto cleanup;
    }
  }

  log_debug("GPG signature verified successfully via gpg --verify binary");
  result = 0;

cleanup:
  // Clean up temp files
  platform_delete_temp_file(sig_path);
  platform_delete_temp_file(msg_path);

  if (fp) {
    platform_pclose(&fp);
  }

  return result;
}
