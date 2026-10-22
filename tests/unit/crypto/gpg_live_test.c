#include <criterion/criterion.h>
#include <ascii-chat/crypto/gpg/signing.h>
#include <ascii-chat/crypto/gpg/verification.h>
#include <ascii-chat/crypto/gpg/export.h>
#include <ascii-chat/crypto/gpg/agent.h>
#include <ascii-chat/crypto/gpg/openpgp.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/util/pcre2.h>
#include <sodium.h>
#include <string.h>
#include <stdlib.h>

static char directory[PLATFORM_MAX_PATH_LENGTH], fingerprint[41];
static char *old_home, *old_path;
static int agent_socket = -1;
static void teardown(void) {
  gpg_agent_disconnect(agent_socket);
  agent_socket = -1;
  if (old_path) {
    platform_setenv("PATH", old_path);
    SAFE_FREE(old_path);
  }
  if (directory[0]) {
    const char *args[] = {"gpgconf", "--homedir", directory, "--kill", "gpg-agent", NULL};
    (void)platform_execute_subprocess("gpgconf", args, NULL, 0);
    platform_rmdir_recursive(directory);
  }
  if (old_home) {
    platform_setenv("GNUPGHOME", old_home);
    SAFE_FREE(old_home);
  } else {
    platform_unsetenv("GNUPGHOME");
  }
  log_destroy();
  asciichat_pcre2_cleanup_all();
}
static void setup(void) {
  if (!platform_is_regular_file("/usr/bin/gpg"))
    cr_skip_test("GnuPG is required for the isolated keyring fixture");
  log_init(NULL, LOG_FATAL, false, false);
  const char *original = getenv("GNUPGHOME");
  if (original)
    SAFE_STRDUP(old_home, original);
  cr_assert_eq(platform_mkdtemp(directory, sizeof(directory), "gpg-live"), ASCIICHAT_OK);
  cr_assert_eq(platform_setenv("GNUPGHOME", directory), 0);
  const char *create[] = {"gpg", "--batch", "--pinentry-mode", "loopback", "--passphrase", "",
                          "--quick-generate-key", "Coverage Fixture <coverage@example.invalid>",
                          "ed25519", "sign,auth", "0", NULL};
  cr_assert_eq(platform_execute_subprocess("gpg", create, NULL, 0), 0);
  const char *list[] = {"gpg", "--batch", "--with-colons", "--list-keys", NULL};
  char output[4096];
  cr_assert_eq(platform_execute_subprocess("gpg", list, output, sizeof(output)), 0);
  char *line = strstr(output, "fpr:");
  cr_assert_not_null(line);
  for (unsigned field = 0; field < 9; ++field) {
    line = strchr(line, ':');
    cr_assert_not_null(line);
    ++line;
  }
  cr_assert_eq(strcspn(line, ":\n"), 40);
  memcpy(fingerprint, line, 40);
  fingerprint[40] = 0;
}
TestSuite(gpg_live, .init = setup, .fini = teardown, .timeout = 30);

Test(gpg_live, packet_signatures_verify_and_reject_altered_messages_signatures_and_signers) {
  uint8_t message[] = {'m', 0, 'e', 0xff, 's', 's', 'a', 'g', 'e'};
  uint8_t signature[512];
  size_t length = 0;
  cr_assert_eq(gpg_sign_with_key(fingerprint, message, sizeof(message), signature, &length), 0);
  cr_assert_gt(length, 64);
  cr_assert_leq(length, sizeof(signature));
  cr_assert_eq(gpg_verify_signature_with_binary(signature, length, message, sizeof(message), fingerprint), 0);
  cr_assert_eq(gpg_verify_signature_with_binary(signature, length, message, sizeof(message), NULL), 0);
  cr_assert_eq(gpg_verify_signature_with_binary(signature, length, message, sizeof(message), ""), 0);
  cr_assert_neq(gpg_verify_signature_with_binary(signature, length, message, sizeof(message),
                                                "0000000000000000000000000000000000000000"), 0);
  message[2] ^= 1;
  cr_assert_neq(gpg_verify_signature_with_binary(signature, length, message, sizeof(message), fingerprint), 0);
  message[2] ^= 1;
  signature[length - 1] ^= 1;
  cr_assert_neq(gpg_verify_signature_with_binary(signature, length, message, sizeof(message), fingerprint), 0);
  cr_assert_neq(gpg_verify_signature_with_binary(signature, 1, message, sizeof(message), fingerprint), 0);
}

Test(gpg_live, exported_public_key_matches_independent_gpg_ssh_export) {
  const char *args[] = {"gpg", "--export-ssh-key", fingerprint, NULL};
  char output[2048];
  cr_assert_eq(platform_execute_subprocess("gpg", args, output, sizeof(output)), 0);
  cr_assert_eq(strncmp(output, "ssh-ed25519 ", 12), 0);
  char *end = strchr(output + 12, ' ');
  cr_assert_not_null(end);
  uint8_t wire[128];
  size_t length = 0;
  cr_assert_eq(sodium_base642bin(wire, sizeof(wire), output + 12, (size_t)(end - output - 12), NULL, &length,
                                NULL, sodium_base64_VARIANT_ORIGINAL), 0);
  cr_assert_eq(length, 51);
  cr_assert_arr_eq(wire + 4, "ssh-ed25519", 11);
  uint8_t public_key[32];
  char grip[41] = {0};
  cr_assert_eq(gpg_get_public_key(fingerprint, public_key, grip), 0);
  cr_assert_arr_eq(public_key, wire + 19, 32);
  cr_assert_eq(strlen(grip), 40);
  uint8_t again[32];
  cr_assert_eq(gpg_get_public_key(fingerprint, again, NULL), 0);
  cr_assert_arr_eq(again, public_key, 32);
  cr_assert_neq(gpg_get_public_key("0000000000000000", again, NULL), 0);
}

Test(gpg_live, invalid_arguments_are_rejected_before_running_gpg) {
  uint8_t message[] = "message", signature[512] = {0};
  size_t length = 999;
  cr_assert_neq(gpg_sign_with_key(NULL, message, sizeof(message), signature, &length), 0);
  cr_assert_neq(gpg_sign_with_key(fingerprint, NULL, sizeof(message), signature, &length), 0);
  cr_assert_neq(gpg_sign_with_key(fingerprint, message, 0, signature, &length), 0);
  cr_assert_neq(gpg_sign_with_key(fingerprint, message, sizeof(message), NULL, &length), 0);
  cr_assert_neq(gpg_sign_with_key(fingerprint, message, sizeof(message), signature, NULL), 0);
  cr_assert_eq(length, 999);
  cr_assert_neq(gpg_verify_signature_with_binary(NULL, 20, message, sizeof(message), NULL), 0);
  cr_assert_neq(gpg_verify_signature_with_binary(signature, 0, message, sizeof(message), NULL), 0);
  cr_assert_neq(gpg_verify_signature_with_binary(signature, 513, message, sizeof(message), NULL), 0);
  cr_assert_neq(gpg_verify_signature_with_binary(signature, 20, NULL, sizeof(message), NULL), 0);
  cr_assert_neq(gpg_verify_signature_with_binary(signature, 20, message, 0, NULL), 0);
}
Test(gpg_live, real_armored_keys_parse_to_the_same_public_key_and_a_usable_ed25519_seed) {
  char armor[8192];
  const char *pubargs[] = {"gpg", "--batch", "--armor", "--export", fingerprint, NULL};
  cr_assert_eq(platform_execute_subprocess("gpg", pubargs, armor, sizeof(armor)), 0);
  uint8_t exported[32], parsed[32], secret_public[32], seed[32];
  cr_assert_eq(gpg_get_public_key(fingerprint, exported, NULL), 0);
  cr_assert_eq(openpgp_parse_armored_pubkey(armor, parsed), ASCIICHAT_OK);
  cr_assert_arr_eq(parsed, exported, 32);
  const char *secargs[] = {"gpg", "--batch", "--pinentry-mode", "loopback", "--passphrase", "",
                           "--armor", "--export-secret-keys", fingerprint, NULL};
  cr_assert_eq(platform_execute_subprocess("gpg", secargs, armor, sizeof(armor)), 0);
  cr_assert_eq(openpgp_parse_armored_seckey(armor, secret_public, seed), ASCIICHAT_OK);
  cr_assert_arr_eq(secret_public, exported, 32);
  uint8_t sodium_public[32], sodium_secret[64], signature[64];
  cr_assert_eq(crypto_sign_seed_keypair(sodium_public, sodium_secret, seed), 0);
  cr_assert_arr_eq(sodium_public, exported, 32);
  const uint8_t message[] = {'s', 'e', 'e', 'd', 0, 0xff};
  cr_assert_eq(crypto_sign_detached(signature, NULL, message, sizeof(message), sodium_secret), 0);
  cr_assert_eq(crypto_sign_verify_detached(signature, message, sizeof(message), exported), 0);
  sodium_memzero(seed, sizeof(seed));
  sodium_memzero(sodium_secret, sizeof(sodium_secret));
  sodium_memzero(armor, sizeof(armor));
}
Test(gpg_live, short_signature_integers_are_restored_to_exactly_32_bytes_each) {
  const char *original = getenv("PATH");
  cr_assert_not_null(original);
  SAFE_STRDUP(old_path, original);
  char script_path[PLATFORM_MAX_PATH_LENGTH], packet_path[PLATFORM_MAX_PATH_LENGTH], path[8192];
  safe_snprintf(script_path, sizeof(script_path), "%s/gpg", directory);
  safe_snprintf(packet_path, sizeof(packet_path), "%s/signature.pgp", directory);
  FILE *file = platform_fopen("gpg-fixture", script_path, "wb");
  cr_assert_not_null(file);
  char script[PLATFORM_MAX_PATH_LENGTH + 80];
  safe_snprintf(script, sizeof(script), "#!/bin/sh\nexec /usr/bin/cp '%s' \"$5\"\n", packet_path);
  cr_assert_eq(fwrite(script, 1, strlen(script), file), strlen(script));
  cr_assert_eq(fclose(file), 0);
  cr_assert_eq(platform_chmod(script_path, 0700), 0);
  safe_snprintf(path, sizeof(path), "%s:%s", directory, old_path);
  cr_assert_eq(platform_setenv("PATH", path), 0);
  const size_t lengths[] = {0, 1, 31, 32, 33};
  const uint8_t message[] = "MPI parser contract";
  for (size_t r = 0; r < 5; ++r) {
    for (size_t s = 0; s < 5; ++s) {
      uint8_t packet[96] = {0x88, 0, 4, 0, 22, 8, 0, 0, 0, 0, 0, 0};
      uint8_t expected[64] = {0}, actual[64];
      size_t offset = 12;
      packet[offset++] = (uint8_t)((lengths[r] * 8) >> 8);
      packet[offset++] = (uint8_t)(lengths[r] * 8);
      for (size_t i = 0; i < lengths[r]; ++i) {
        packet[offset++] = (uint8_t)(0x80 + i);
        if (lengths[r] <= 32)
          expected[32 - lengths[r] + i] = (uint8_t)(0x80 + i);
      }
      packet[offset++] = (uint8_t)((lengths[s] * 8) >> 8);
      packet[offset++] = (uint8_t)(lengths[s] * 8);
      for (size_t i = 0; i < lengths[s]; ++i) {
        packet[offset++] = (uint8_t)(0x90 + i);
        if (lengths[s] <= 32)
          expected[64 - lengths[s] + i] = (uint8_t)(0x90 + i);
      }
      packet[1] = (uint8_t)(offset - 2);
      file = platform_fopen("gpg-packet", packet_path, "wb");
      cr_assert_not_null(file);
      cr_assert_eq(fwrite(packet, 1, offset, file), offset);
      cr_assert_eq(fclose(file), 0);
      memset(actual, 0xa5, sizeof(actual));
      if (lengths[r] == 0 || lengths[r] > 32 || lengths[s] == 0 || lengths[s] > 32) {
        cr_assert_neq(gpg_sign_detached_ed25519(fingerprint, message, sizeof(message), actual), 0);
        continue;
      }
      cr_assert_eq(gpg_sign_detached_ed25519(fingerprint, message, sizeof(message), actual), 0,
                   "R length %zu, S length %zu", lengths[r], lengths[s]);
      cr_assert_arr_eq(actual, expected, sizeof(actual));
    }
  }
}
Test(gpg_live, agent_signature_verifies_independently_over_the_documented_sha512_digest) {
  uint8_t public_key[32], signature[64], digest[crypto_hash_sha512_BYTES];
  char grip[41];
  cr_assert_eq(gpg_get_public_key(fingerprint, public_key, grip), 0);
  cr_assert(gpg_agent_is_available());
  agent_socket = gpg_agent_connect();
  cr_assert_geq(agent_socket, 0);
  const uint8_t message[] = {'a', 'g', 'e', 'n', 't', 0, 0xff};
  size_t signature_length = 0;
  cr_assert_eq(gpg_agent_sign(agent_socket, grip, message, sizeof(message), signature, &signature_length), 0);
  cr_assert_eq(signature_length, 64);
  cr_assert_eq(crypto_hash_sha512(digest, message, sizeof(message)), 0);
  cr_assert_eq(crypto_sign_verify_detached(signature, digest, sizeof(digest), public_key), 0);
  digest[0] ^= 1;
  cr_assert_neq(crypto_sign_verify_detached(signature, digest, sizeof(digest), public_key), 0);
}
static socket_t wire_client = INVALID_SOCKET_VALUE, wire_peer = INVALID_SOCKET_VALUE;
static void wire_setup(void) { log_init(NULL, LOG_FATAL, false, false); }
static void wire_cleanup(void) {
  if (wire_client != INVALID_SOCKET_VALUE)
    socket_close(wire_client);
  if (wire_peer != INVALID_SOCKET_VALUE)
    socket_close(wire_peer);
  wire_client = wire_peer = INVALID_SOCKET_VALUE;
  log_destroy();
  asciichat_pcre2_cleanup_all();
}
TestSuite(gpg_agent_wire, .init = wire_setup, .fini = wire_cleanup, .timeout = 5);
Test(gpg_agent_wire, binary_nulls_and_percent_escapes_preserve_signature_bytes_and_bad_frames_are_rejected) {
  for (unsigned malformed = 0; malformed < 3; ++malformed) {
    socket_t listener = socket_create("gpg-wire-listen", AF_INET, SOCK_STREAM, 0);
    cr_assert_neq(listener, INVALID_SOCKET_VALUE);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    cr_assert_eq(socket_bind(listener, (struct sockaddr *)&address, sizeof(address)), 0);
    cr_assert_eq(socket_listen(listener, 1), 0);
    socklen_t address_len = sizeof(address);
    cr_assert_eq(socket_getsockname(listener, (struct sockaddr *)&address, &address_len), 0);
    wire_client = socket_create("gpg-wire-client", AF_INET, SOCK_STREAM, 0);
    cr_assert_neq(wire_client, INVALID_SOCKET_VALUE);
    cr_assert_eq(socket_connect(wire_client, (struct sockaddr *)&address, address_len), 0);
    wire_peer = socket_accept(listener, NULL, NULL, "gpg-wire-peer");
    socket_close(listener);
    cr_assert_neq(wire_peer, INVALID_SOCKET_VALUE);
    uint8_t expected[64];
    for (unsigned i = 0; i < sizeof(expected); ++i)
      expected[i] = (uint8_t)i;
    expected[4] = '%';
    uint8_t sexp[128];
    const char prefix[] = "(7:sig-val(5:eddsa(1:r32:";
    size_t length = sizeof(prefix) - 1;
    memcpy(sexp, prefix, length);
    memcpy(sexp + length, expected, 32);
    length += 32;
    memcpy(sexp + length, ")(1:s32:", 8);
    length += 8;
    memcpy(sexp + length, expected + 32, 32);
    length += 32;
    memcpy(sexp + length, ")))", 3);
    length += 3;
    if (malformed == 2)
      --length;
    uint8_t response[512];
    size_t used = 8;
    memcpy(response, "OK\nOK\nD ", used);
    const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; ++i) {
      if (sexp[i] == '%' || sexp[i] == '\r' || sexp[i] == '\n') {
        response[used++] = '%';
        response[used++] = malformed == 1 ? 'Z' : (uint8_t)hex[sexp[i] >> 4];
        response[used++] = (uint8_t)hex[sexp[i] & 15];
      } else {
        response[used++] = sexp[i];
      }
    }
    memcpy(response + used, "\nOK\n", 4);
    used += 4;
    cr_assert_eq(socket_send(wire_peer, response, used, 0), (ssize_t)used);
    uint8_t actual[64];
    memset(actual, 0xa5, sizeof(actual));
    size_t signature_len = 999;
    const uint8_t message[] = "wire";
    int result = gpg_agent_sign((int)wire_client, "0123456789012345678901234567890123456789",
                                message, sizeof(message), actual, &signature_len);
    if (malformed) {
      cr_assert_neq(result, 0);
      cr_assert_eq(signature_len, 999);
      for (size_t i = 0; i < sizeof(actual); ++i)
        cr_assert_eq(actual[i], 0xa5);
    } else {
      cr_assert_eq(result, 0);
      cr_assert_eq(signature_len, 64);
      cr_assert_arr_eq(actual, expected, 64);
    }
    socket_close(wire_client);
    socket_close(wire_peer);
    wire_client = wire_peer = INVALID_SOCKET_VALUE;
  }
}
Test(gpg_live, public_key_export_falls_back_to_packet_parsing_when_the_isolated_agent_stops) {
  uint8_t expected[32], actual[32];
  char grip[41];
  cr_assert_eq(gpg_get_public_key(fingerprint, expected, grip), 0);
  const char *stop[] = {"gpgconf", "--homedir", directory, "--kill", "gpg-agent", NULL};
  cr_assert_eq(platform_execute_subprocess("gpgconf", stop, NULL, 0), 0);
  for (unsigned attempt = 0; attempt < 100 && gpg_agent_is_available(); ++attempt)
    platform_sleep_us(10000);
  cr_assert_not(gpg_agent_is_available());
  memset(actual, 0xa5, sizeof(actual));
  cr_assert_eq(gpg_get_public_key(fingerprint, actual, NULL), 0);
  cr_assert_arr_eq(actual, expected, sizeof(actual));
}