#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/crypto/gpg/export.h>
#include <ascii-chat/crypto/gpg/gpg_keys.h>
#include <ascii-chat/crypto/gpg/openpgp.h>
#include <ascii-chat/crypto/gpg/signing.h>
#include <ascii-chat/platform/filesystem.h>
#include <libplatform/system.h>
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
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("gpg", generate, NULL, 0, NULL, NULL), 0);
  const char *list[] = {"gpg", "--with-colons", "--list-keys", identity, NULL};
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("gpg", list, output, sizeof(output), NULL, NULL), 0);
  char *field = strstr(output, "fpr:");
  ASCIICHAT_REQUIRE_NOT_NULL(field);
  for (int i = 0; i < 9; ++i) {
    field = strchr(field, ':');
    ASCIICHAT_REQUIRE_NOT_NULL(field);
    ++field;
  }
  memcpy(fingerprint, field, 40);
  fingerprint[40] = 0;
}

static void setup(void) {
  const char *version[] = {"gpg", "--version", NULL};
  char output[8192];
  if (platform_execute_subprocess("gpg", version, output, sizeof(output), NULL, NULL) != 0)
    ASCIICHAT_SKIP("GnuPG is required for isolated keyring integration");
  ASCIICHAT_REQUIRE_EQ(platform_mkdtemp(home, sizeof(home), "ascii-gpg-test"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(platform_setenv("GNUPGHOME", home), 0);
  generate_key(0);
}

static void teardown(void) {
  if (home[0]) {
    const char *stop[] = {"gpgconf", "--kill", "all", NULL};
    platform_execute_subprocess("gpgconf", stop, NULL, 0, NULL, NULL);
    platform_unsetenv("GNUPGHOME");
    platform_rmdir_recursive(home);
    home[0] = 0;
  }
}
TEST_CASE("gpg_keyring/exported_public_and_private_packets_reconstruct_the_same_ed25519_identity", "[gpg_keyring]") {
  uint8_t exported[32], armored_public[32], private_public[32], seed[32], regenerated[32], secret[64];
  char grip[41] = {0}, armor[16384];
  bool escaped = false;
  for (unsigned int attempt = 0; attempt < 64 && !escaped; ++attempt) {
    if (attempt)
      generate_key(attempt);
    const char *public_args[] = {"gpg", "--armor", "--export", fingerprint, NULL};
    ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("gpg", public_args, armor, sizeof(armor), NULL, NULL), 0);
    ASCIICHAT_REQUIRE_EQ(openpgp_parse_armored_pubkey(armor, armored_public), ASCIICHAT_OK);
    for (size_t i = 0; i < sizeof(armored_public); ++i)
      escaped |= armored_public[i] == '%' || armored_public[i] == '\r' || armored_public[i] == '\n';
  }
  ASCIICHAT_REQUIRE(escaped, "Fixture must exercise Assuan escaping in the binary public key");
  ASCIICHAT_REQUIRE_EQ(gpg_get_public_key(fingerprint, exported, grip), 0);
  ASCIICHAT_REQUIRE_EQ(strlen(grip), 40);
  ASCIICHAT_REQUIRE_ARRAY_EQ(exported, armored_public, 32);
  const char *private_args[] = {"gpg", "--batch", "--armor", "--export-secret-keys", fingerprint, NULL};
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("gpg", private_args, armor, sizeof(armor), NULL, NULL), 0);
  ASCIICHAT_REQUIRE_EQ(openpgp_parse_armored_seckey(armor, private_public, seed), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_ARRAY_EQ(private_public, exported, 32);
  ASCIICHAT_REQUIRE_EQ(crypto_sign_seed_keypair(regenerated, secret, seed), 0);
  ASCIICHAT_REQUIRE_ARRAY_EQ(regenerated, exported, 32);
  bool expired = true;
  ASCIICHAT_REQUIRE_EQ(check_gpg_key_expiry(fingerprint, &expired), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(expired);
  sodium_memzero(seed, sizeof(seed));
  sodium_memzero(secret, sizeof(secret));
}

TEST_CASE("gpg_keyring/native_detached_signature_is_accepted_by_gnupg_and_tampering_is_rejected", "[gpg_keyring]") {
  const uint8_t message[] = "an isolated ascii-chat authentication message";
  uint8_t signature[4096], raw[64];
  size_t size = 0;
  ASCIICHAT_REQUIRE_EQ(gpg_sign_with_key(fingerprint, message, sizeof(message) - 1, signature, &size), 0);
  ASCIICHAT_REQUIRE_GT(size, 64);
  char message_path[256], signature_path[256];
  int message_fd, signature_fd;
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(message_path, sizeof(message_path), "ascii-gpg-message", &message_fd), 0);
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(signature_path, sizeof(signature_path), "ascii-gpg-signature", &signature_fd),
               0);
  ASCIICHAT_REQUIRE_EQ(platform_write(message_fd, message, sizeof(message) - 1), (ssize_t)sizeof(message) - 1);
  ASCIICHAT_REQUIRE_EQ(platform_write(signature_fd, signature, size), (ssize_t)size);
  platform_close(message_fd);
  platform_close(signature_fd);
  const char *verify[] = {"gpg", "--batch", "--verify", signature_path, message_path, NULL};
  ASCIICHAT_REQUIRE_EQ(platform_execute_subprocess("gpg", verify, NULL, 0, NULL, NULL), 0);
  FILE *file = platform_fopen("tamper-fixture", message_path, "wb");
  ASCIICHAT_REQUIRE_NOT_NULL(file);
  ASCIICHAT_REQUIRE_EQ(fwrite("tampered", 1, 8, file), 8);
  fclose(file);
  ASCIICHAT_REQUIRE_NE(platform_execute_subprocess("gpg", verify, NULL, 0, NULL, NULL), 0);
  ASCIICHAT_REQUIRE_EQ(gpg_sign_detached_ed25519(fingerprint, message, sizeof(message) - 1, raw), 0);
  ASCIICHAT_REQUIRE_NOT(sodium_is_zero(raw, sizeof(raw)));
  platform_delete_temp_file(message_path);
  platform_delete_temp_file(signature_path);
}
