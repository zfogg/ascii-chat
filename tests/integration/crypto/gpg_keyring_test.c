#include <criterion/criterion.h>
#include <ascii-chat/crypto/gpg/export.h>
#include <ascii-chat/crypto/gpg/gpg_keys.h>
#include <ascii-chat/crypto/gpg/openpgp.h>
#include <ascii-chat/crypto/gpg/signing.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <sodium.h>
#include <string.h>

static char home[256], fingerprint[41];

static void generate_key(unsigned int sequence) {
  char output[8192];
  char identity[80];
  safe_snprintf(identity, sizeof(identity), "coverage-%u@example.invalid", sequence);
  const char *generate[] = {"gpg",
                            "--batch",
                            "--pinentry-mode",
                            "loopback",
                            "--passphrase",
                            "",
                            "--quick-generate-key",
                            identity,
                            "ed25519",
                            "sign",
                            "0",
                            NULL};
  cr_assert_eq(platform_execute_subprocess("gpg", generate, NULL, 0), 0);
  const char *list[] = {"gpg", "--with-colons", "--list-keys", identity, NULL};
  cr_assert_eq(platform_execute_subprocess("gpg", list, output, sizeof(output)), 0);
  char *field = strstr(output, "fpr:");
  cr_assert_not_null(field);
  for (int i = 0; i < 9; ++i) {
    field = strchr(field, ':');
    cr_assert_not_null(field);
    ++field;
  }
  memcpy(fingerprint, field, 40);
  fingerprint[40] = 0;
}

static void setup(void) {
  const char *version[] = {"gpg", "--version", NULL};
  char output[8192];
  if (platform_execute_subprocess("gpg", version, output, sizeof(output)) != 0)
    cr_skip_test("GnuPG is required for isolated keyring integration");
  cr_assert_eq(platform_mkdtemp(home, sizeof(home), "ascii-gpg-test"), ASCIICHAT_OK);
  cr_assert_eq(platform_setenv("GNUPGHOME", home), 0);
  generate_key(0);
}

static void teardown(void) {
  if (home[0]) {
    const char *stop[] = {"gpgconf", "--kill", "all", NULL};
    platform_execute_subprocess("gpgconf", stop, NULL, 0);
    platform_unsetenv("GNUPGHOME");
    platform_rmdir_recursive(home);
    home[0] = 0;
  }
}

TestSuite(gpg_keyring, .init = setup, .fini = teardown, .timeout = 30);

Test(gpg_keyring, exported_public_and_private_packets_reconstruct_the_same_ed25519_identity) {
  uint8_t exported[32], armored_public[32], private_public[32], seed[32], regenerated[32], secret[64];
  char grip[41] = {0}, armor[16384];
  bool escaped = false;
  for (unsigned int attempt = 0; attempt < 64 && !escaped; ++attempt) {
    if (attempt)
      generate_key(attempt);
    const char *public_args[] = {"gpg", "--armor", "--export", fingerprint, NULL};
    cr_assert_eq(platform_execute_subprocess("gpg", public_args, armor, sizeof(armor)), 0);
    cr_assert_eq(openpgp_parse_armored_pubkey(armor, armored_public), ASCIICHAT_OK);
    for (size_t i = 0; i < sizeof(armored_public); ++i)
      escaped |= armored_public[i] == '%' || armored_public[i] == '\r' || armored_public[i] == '\n';
  }
  cr_assert(escaped, "Fixture must exercise Assuan escaping in the binary public key");
  cr_assert_eq(gpg_get_public_key(fingerprint, exported, grip), 0);
  cr_assert_eq(strlen(grip), 40);
  cr_assert_arr_eq(exported, armored_public, 32);
  const char *private_args[] = {"gpg", "--batch", "--armor", "--export-secret-keys", fingerprint, NULL};
  cr_assert_eq(platform_execute_subprocess("gpg", private_args, armor, sizeof(armor)), 0);
  cr_assert_eq(openpgp_parse_armored_seckey(armor, private_public, seed), ASCIICHAT_OK);
  cr_assert_arr_eq(private_public, exported, 32);
  cr_assert_eq(crypto_sign_seed_keypair(regenerated, secret, seed), 0);
  cr_assert_arr_eq(regenerated, exported, 32);
  bool expired = true;
  cr_assert_eq(check_gpg_key_expiry(fingerprint, &expired), ASCIICHAT_OK);
  cr_assert_not(expired);
  sodium_memzero(seed, sizeof(seed));
  sodium_memzero(secret, sizeof(secret));
}

Test(gpg_keyring, native_detached_signature_is_accepted_by_gnupg_and_tampering_is_rejected) {
  const uint8_t message[] = "an isolated ascii-chat authentication message";
  uint8_t signature[4096], raw[64];
  size_t size = 0;
  cr_assert_eq(gpg_sign_with_key(fingerprint, message, sizeof(message) - 1, signature, &size), 0);
  cr_assert_gt(size, 64);
  char message_path[256], signature_path[256];
  int message_fd, signature_fd;
  cr_assert_eq(platform_create_temp_file(message_path, sizeof(message_path), "ascii-gpg-message", &message_fd), 0);
  cr_assert_eq(platform_create_temp_file(signature_path, sizeof(signature_path), "ascii-gpg-signature", &signature_fd),
               0);
  cr_assert_eq(platform_write(message_fd, message, sizeof(message) - 1), (ssize_t)sizeof(message) - 1);
  cr_assert_eq(platform_write(signature_fd, signature, size), (ssize_t)size);
  platform_close(message_fd);
  platform_close(signature_fd);
  const char *verify[] = {"gpg", "--batch", "--verify", signature_path, message_path, NULL};
  cr_assert_eq(platform_execute_subprocess("gpg", verify, NULL, 0), 0);
  FILE *file = platform_fopen("tamper-fixture", message_path, "wb");
  cr_assert_not_null(file);
  cr_assert_eq(fwrite("tampered", 1, 8, file), 8);
  fclose(file);
  cr_assert_neq(platform_execute_subprocess("gpg", verify, NULL, 0), 0);
  cr_assert_eq(gpg_sign_detached_ed25519(fingerprint, message, sizeof(message) - 1, raw), 0);
  cr_assert_not(sodium_is_zero(raw, sizeof(raw)));
  platform_delete_temp_file(message_path);
  platform_delete_temp_file(signature_path);
}
