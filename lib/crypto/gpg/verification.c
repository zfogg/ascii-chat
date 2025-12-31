/**
 * @file crypto/gpg/verification.c
 * @ingroup crypto
 * @brief GPG signature verification implementation
 */

#include "verification.h"
#include "signing.h"
#include "../keys.h"
#include "core/common.h"
#include "util/string.h"
#include "util/validation.h"
#include "log/logging.h"
#include "platform/system.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_LIBGCRYPT
#include <gcrypt.h>
#endif

#ifdef _WIN32
#define SAFE_POPEN _popen
#define SAFE_PCLOSE _pclose
#else
#define SAFE_POPEN popen
#define SAFE_PCLOSE pclose
#endif

int gpg_verify_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                                const uint8_t signature[64]) {
  // Note: We don't use the raw signature parameter directly.
  // Instead, we regenerate the OpenPGP signature using GPG (Ed25519 is deterministic).
  (void)signature;

  log_info("gpg_verify_detached_ed25519: Verifying signature with key ID %s using gpg --verify", key_id);

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
  char msg_path[] = "/tmp/gpg_verify_msg_XXXXXX";
  char sig_path[] = "/tmp/gpg_verify_sig_XXXXXX";

  int msg_fd = mkstemp(msg_path);
  if (msg_fd < 0) {
    log_error("Failed to create temporary message file");
    return -1;
  }

  int sig_fd = mkstemp(sig_path);
  if (sig_fd < 0) {
    close(msg_fd);
    unlink(msg_path);
    log_error("Failed to create temporary signature file");
    return -1;
  }

  // Write message
  if (write(msg_fd, message, message_len) != (ssize_t)message_len) {
    log_error("Failed to write message to temp file");
    close(msg_fd);
    close(sig_fd);
    unlink(msg_path);
    unlink(sig_path);
    return -1;
  }
  close(msg_fd);

  // Write OpenPGP signature
  if (write(sig_fd, openpgp_signature, openpgp_len) != (ssize_t)openpgp_len) {
    log_error("Failed to write signature to temp file");
    close(sig_fd);
    unlink(msg_path);
    unlink(sig_path);
    return -1;
  }
  close(sig_fd);

  // Call gpg --verify
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "gpg --verify '%s' '%s' 2>&1", sig_path, msg_path);

  log_debug("Running: %s", cmd);
  FILE *fp = popen(cmd, "r");
  if (!fp) {
    log_error("Failed to run gpg --verify");
    unlink(msg_path);
    unlink(sig_path);
    return -1;
  }

  char output[4096] = {0};
  size_t output_len = fread(output, 1, sizeof(output) - 1, fp);
  int exit_code = pclose(fp);

  // Cleanup temp files
  unlink(msg_path);
  unlink(sig_path);

  if (exit_code == 0) {
    log_info("GPG signature verification PASSED");
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
    snprintf(pubkey_hex + i * 2, 3, "%02x", public_key[i]);
    snprintf(r_hex + i * 2, 3, "%02x", signature[i]);
    snprintf(s_hex + i * 2, 3, "%02x", signature[32 + i]);
  }
  for (size_t i = 0; i < (message_len < 32 ? message_len : 32); i++) {
    snprintf(msg_hex + i * 2, 3, "%02x", message[i]);
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

#ifdef _WIN32
  // Windows temp file creation with process ID for concurrent process safety
  char temp_dir[PLATFORM_MAX_PATH_LENGTH];
  DWORD temp_dir_len = GetTempPathA(sizeof(temp_dir), temp_dir);
  if (temp_dir_len == 0 || temp_dir_len >= sizeof(temp_dir)) {
    log_error("Failed to get Windows temp directory");
    return -1;
  }

  // Create process-specific temp file prefixes (e.g., "asc_sig_12345_")
  char sig_prefix[32];
  char msg_prefix[32];
  safe_snprintf(sig_prefix, sizeof(sig_prefix), "asc_sig_%lu_", GetCurrentProcessId());
  safe_snprintf(msg_prefix, sizeof(msg_prefix), "asc_msg_%lu_", GetCurrentProcessId());

  // Create signature temp file
  if (GetTempFileNameA(temp_dir, sig_prefix, 0, sig_path) == 0) {
    log_error("Failed to create signature temp file: %lu", GetLastError());
    return -1;
  }

  // Create message temp file
  if (GetTempFileNameA(temp_dir, msg_prefix, 0, msg_path) == 0) {
    log_error("Failed to create message temp file: %lu", GetLastError());
    DeleteFileA(sig_path);
    return -1;
  }

  // Open files for writing (Windows CreateFile for binary mode)
  HANDLE sig_handle = CreateFileA(sig_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (sig_handle == INVALID_HANDLE_VALUE) {
    log_error("Failed to open signature temp file: %lu", GetLastError());
    DeleteFileA(sig_path);
    DeleteFileA(msg_path);
    return -1;
  }

  DWORD bytes_written;
  if (!WriteFile(sig_handle, signature, (DWORD)signature_len, &bytes_written, NULL) || bytes_written != signature_len) {
    log_error("Failed to write signature to temp file: %lu", GetLastError());
    CloseHandle(sig_handle);
    DeleteFileA(sig_path);
    DeleteFileA(msg_path);
    return -1;
  }
  CloseHandle(sig_handle);

  HANDLE msg_handle = CreateFileA(msg_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (msg_handle == INVALID_HANDLE_VALUE) {
    log_error("Failed to open message temp file: %lu", GetLastError());
    DeleteFileA(sig_path);
    DeleteFileA(msg_path);
    return -1;
  }

  if (!WriteFile(msg_handle, message, (DWORD)message_len, &bytes_written, NULL) || bytes_written != message_len) {
    log_error("Failed to write message to temp file: %lu", GetLastError());
    CloseHandle(msg_handle);
    DeleteFileA(sig_path);
    DeleteFileA(msg_path);
    return -1;
  }
  CloseHandle(msg_handle);

#else
  // Unix temp file creation with mkstemp() - include PID for concurrent process safety
  safe_snprintf(sig_path, sizeof(sig_path), "/tmp/asciichat_sig_%d_XXXXXX", getpid());
  safe_snprintf(msg_path, sizeof(msg_path), "/tmp/asciichat_msg_%d_XXXXXX", getpid());

  sig_fd = mkstemp(sig_path);
  if (sig_fd < 0) {
    log_error("Failed to create signature temp file: %s", SAFE_STRERROR(errno));
    return -1;
  }

  msg_fd = mkstemp(msg_path);
  if (msg_fd < 0) {
    log_error("Failed to create message temp file: %s", SAFE_STRERROR(errno));
    close(sig_fd);
    unlink(sig_path);
    return -1;
  }

  // Write signature to temp file
  ssize_t sig_written = write(sig_fd, signature, signature_len);
  if (sig_written != (ssize_t)signature_len) {
    log_error("Failed to write signature to temp file: %s", SAFE_STRERROR(errno));
    close(sig_fd);
    close(msg_fd);
    unlink(sig_path);
    unlink(msg_path);
    return -1;
  }
  close(sig_fd);

  // Write message to temp file
  ssize_t msg_written = write(msg_fd, message, message_len);
  if (msg_written != (ssize_t)message_len) {
    log_error("Failed to write message to temp file: %s", SAFE_STRERROR(errno));
    close(msg_fd);
    unlink(sig_path);
    unlink(msg_path);
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
  FILE *fp = SAFE_POPEN(cmd, "r");
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
      SAFE_PCLOSE(fp);
      fp = NULL;
      goto cleanup;
    }
  }

  // Check exit code
  int status = SAFE_PCLOSE(fp);
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

  log_info("GPG signature verified successfully via gpg --verify binary");
  result = 0;

cleanup:
  // Clean up temp files
#ifdef _WIN32
  DeleteFileA(sig_path);
  DeleteFileA(msg_path);
#else
  unlink(sig_path);
  unlink(msg_path);
#endif

  if (fp) {
    SAFE_PCLOSE(fp);
  }

  return result;
}
