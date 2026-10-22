#include <criterion/criterion.h>
#include <ascii-chat/crypto/gpg/signing.h>
#include <ascii-chat/crypto/gpg/verification.h>
#include <ascii-chat/crypto/gpg/export.h>
#include <ascii-chat/crypto/gpg/openpgp.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/tests/logging.h>
#include <sodium.h>
#include <string.h>
#include <stdlib.h>

static char directory[PLATFORM_MAX_PATH_LENGTH], fingerprint[41];
static char *old_home;
static void teardown(void) {
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