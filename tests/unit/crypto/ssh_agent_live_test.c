#include <criterion/criterion.h>
#include <ascii-chat/crypto/ssh/ssh_agent.h>
#include <ascii-chat/crypto/ssh/ssh_keys.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/tests/logging.h>
#include <sodium.h>
#include <string.h>

static bool agent_started;
static char directory[PLATFORM_MAX_PATH_LENGTH], socket_path[PLATFORM_MAX_PATH_LENGTH];
static char *old_socket;
static char *old_password;
static char *old_agent_pid;

static void teardown(void) {
  if (agent_started) {
    const char *args[] = {"ssh-agent", "-k", NULL};
    char output[512];
    platform_execute_subprocess("/usr/bin/ssh-agent", args, output, sizeof(output));
    agent_started = false;
  }
  if (old_agent_pid) {
    platform_setenv("SSH_AGENT_PID", old_agent_pid);
    SAFE_FREE(old_agent_pid);
  } else {
    platform_unsetenv("SSH_AGENT_PID");
  }
  if (old_socket) {
    platform_setenv("SSH_AUTH_SOCK", old_socket);
    SAFE_FREE(old_socket);
  } else {
    platform_unsetenv("SSH_AUTH_SOCK");
  }
  if (old_password) {
    platform_setenv("ASCII_CHAT_KEY_PASSWORD", old_password);
    SAFE_FREE(old_password);
  } else {
    platform_unsetenv("ASCII_CHAT_KEY_PASSWORD");
  }
  if (directory[0])
    platform_rmdir_recursive(directory);
  log_destroy();
}

static void setup(void) {
  if (!platform_is_regular_file("/usr/bin/ssh-agent"))
    cr_skip_test("OpenSSH ssh-agent is required for this isolated protocol fixture");
  log_init(NULL, LOG_FATAL, false, false);
  const char *original = getenv("SSH_AUTH_SOCK");
  old_socket = NULL;
  if (original)
    SAFE_STRDUP(old_socket, original);
  original = getenv("ASCII_CHAT_KEY_PASSWORD");
  old_password = NULL;
  if (original)
    SAFE_STRDUP(old_password, original);
  original = getenv("SSH_AGENT_PID");
  old_agent_pid = NULL;
  if (original)
    SAFE_STRDUP(old_agent_pid, original);
  cr_assert_eq(platform_mkdtemp(directory, sizeof(directory), "agent-live"), ASCIICHAT_OK);
  safe_snprintf(socket_path, sizeof(socket_path), "%s/socket", directory);
  cr_assert_eq(platform_setenv("SSH_AUTH_SOCK", socket_path), 0);
  const char *args[] = {"ssh-agent", "-s", "-a", socket_path, NULL};
  char output[1024];
  cr_assert_eq(platform_execute_subprocess("/usr/bin/ssh-agent", args, output, sizeof(output)), 0);
  char *pid_start = strstr(output, "SSH_AGENT_PID=");
  cr_assert_not_null(pid_start);
  pid_start += strlen("SSH_AGENT_PID=");
  char *pid_end = strchr(pid_start, ';');
  cr_assert_not_null(pid_end);
  *pid_end = '\0';
  cr_assert_eq(platform_setenv("SSH_AGENT_PID", pid_start), 0);
  agent_started = true;
  for (unsigned attempt = 0; attempt < 200; ++attempt) {
    if (ssh_agent_is_available())
      return;
    platform_sleep_us(10000);
  }
  cr_assert_fail("Isolated ssh-agent did not become ready within two seconds");
}

TestSuite(ssh_agent_live, .init = setup, .fini = teardown, .timeout = 15);

static void make_key(private_key_t *secret, public_key_t *public, uint8_t seed_byte) {
  memset(secret, 0, sizeof(*secret));
  memset(public, 0, sizeof(*public));
  secret->type = public->type = KEY_TYPE_ED25519;
  uint8_t seed[32];
  memset(seed, seed_byte, sizeof(seed));
  cr_assert_eq(crypto_sign_seed_keypair(public->key, secret->key.ed25519, seed), 0);
  memcpy(secret->public_key, public->key, 32);
  sodium_memzero(seed, sizeof(seed));
}

Test(ssh_agent_live, empty_agent_then_add_list_and_sign_with_independent_verification) {
  private_key_t secret;
  public_key_t public;
  make_key(&secret, &public, 7);
  cr_assert_not(ssh_agent_has_key(&public));
  cr_assert_eq(ssh_agent_add_key(&secret, "isolated-test-key"), ASCIICHAT_OK);
  cr_assert(ssh_agent_has_key(&public));
  cr_assert_eq(ssh_agent_add_key(&secret, NULL), ASCIICHAT_OK);
  cr_assert(ssh_agent_has_key(&public));
  const uint8_t message[] = {'a', 0, 'b', 0xff};
  uint8_t signature[64], direct[64];
  cr_assert_eq(ssh_agent_sign(&public, message, sizeof(message), signature), ASCIICHAT_OK);
  cr_assert_eq(crypto_sign_verify_detached(signature, message, sizeof(message), public.key), 0);
  cr_assert_eq(crypto_sign_detached(direct, NULL, message, sizeof(message), secret.key.ed25519), 0);
  cr_assert_arr_eq(signature, direct, sizeof(signature));
  uint8_t altered[] = {'a', 0, 'c', 0xff};
  cr_assert_neq(crypto_sign_verify_detached(signature, altered, sizeof(altered), public.key), 0);
  sodium_memzero(&secret, sizeof(secret));
}

Test(ssh_agent_live, signing_selects_the_requested_key_and_supports_agent_backed_private_keys) {
  private_key_t first, second;
  public_key_t first_public, second_public;
  make_key(&first, &first_public, 10);
  make_key(&second, &second_public, 20);
  cr_assert_eq(ssh_agent_add_key(&first, "first"), ASCIICHAT_OK);
  cr_assert_eq(ssh_agent_add_key(&second, "second"), ASCIICHAT_OK);
  cr_assert(ssh_agent_has_key(&first_public));
  cr_assert(ssh_agent_has_key(&second_public));
  const uint8_t message[] = "agent-selection";
  uint8_t signature[64];
  first.use_ssh_agent = true;
  sodium_memzero(first.key.ed25519, sizeof(first.key.ed25519));
  cr_assert_eq(ed25519_sign_message(&first, message, sizeof(message), signature), ASCIICHAT_OK);
  cr_assert_eq(crypto_sign_verify_detached(signature, message, sizeof(message), first_public.key), 0);
  cr_assert_neq(crypto_sign_verify_detached(signature, message, sizeof(message), second_public.key), 0);
  cr_assert_eq(ssh_agent_sign(&second_public, message, 0, signature), ASCIICHAT_OK);
  cr_assert_eq(crypto_sign_verify_detached(signature, message, 0, second_public.key), 0);
  sodium_memzero(&first, sizeof(first));
  sodium_memzero(&second, sizeof(second));
}

Test(ssh_agent_live, missing_key_refusal_does_not_publish_a_signature) {
  private_key_t secret;
  public_key_t public;
  make_key(&secret, &public, 30);
  const uint8_t message[] = "missing-key";
  uint8_t signature[64];
  memset(signature, 0xa5, sizeof(signature));
  cr_assert_eq(ssh_agent_sign(&public, message, sizeof(message), signature), ERROR_CRYPTO);
  for (size_t i = 0; i < sizeof(signature); ++i)
    cr_assert_eq(signature[i], 0xa5);
  char oversized[BUFFER_SIZE_XXLARGE + 1];
  memset(oversized, 'x', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = '\0';
  cr_assert_eq(ssh_agent_add_key(&secret, oversized), ERROR_BUFFER_OVERFLOW);
  cr_assert_not(ssh_agent_has_key(&public));
  cr_assert_eq(ssh_agent_add_key(&secret, "after-refusal"), ASCIICHAT_OK);
  cr_assert(ssh_agent_has_key(&public));
  sodium_memzero(&secret, sizeof(secret));
}

Test(ssh_agent_live, openssh_generated_plain_and_encrypted_keys_sign_the_matching_public_key) {
  const char *ciphers[] = {"none", "aes256-ctr", "aes256-cbc"};
  for (size_t index = 0; index < sizeof(ciphers) / sizeof(ciphers[0]); ++index) {
    char private_path[PLATFORM_MAX_PATH_LENGTH], public_path[PLATFORM_MAX_PATH_LENGTH];
    safe_snprintf(private_path, sizeof(private_path), "%s/id-%zu", directory, index);
    safe_snprintf(public_path, sizeof(public_path), "%s.pub", private_path);
    const char *password = index ? "fixture-passphrase" : "";
    const char *args[] = {"ssh-keygen", "-q",           "-t", "ed25519",         "-a", "1",          "-N", password,
                          "-Z",         ciphers[index], "-C", "fixture-comment", "-f", private_path, NULL};
    cr_assert_eq(platform_execute_subprocess("/usr/bin/ssh-keygen", args, NULL, 0), 0);
    cr_assert_eq(platform_setenv("ASCII_CHAT_KEY_PASSWORD", password), 0);
    public_key_t public;
    private_key_t secret;
    cr_assert_eq(parse_public_key(public_path, &public), ASCIICHAT_OK);
    cr_assert_eq(validate_ssh_key_file(private_path), ASCIICHAT_OK);
    memset(&secret, 0xa5, sizeof(secret));
    cr_assert_eq(parse_ssh_private_key(private_path, &secret), ASCIICHAT_OK, "%s", ciphers[index]);
    cr_assert_eq(secret.type, KEY_TYPE_ED25519);
    cr_assert_not(secret.use_ssh_agent);
    cr_assert_not(secret.use_gpg_agent);
    cr_assert_arr_eq(secret.public_key, public.key, sizeof(public.key));
    const uint8_t message[] = "OpenSSH interoperability";
    uint8_t signature[64];
    cr_assert_eq(ed25519_sign_message(&secret, message, sizeof(message), signature), ASCIICHAT_OK);
    cr_assert_eq(crypto_sign_verify_detached(signature, message, sizeof(message), public.key), 0);
    cr_assert_eq(ed25519_verify_signature(public.key, message, sizeof(message), signature, NULL), ASCIICHAT_OK);
    uint8_t converted_public[32], converted_secret[32], derived_public[32];
    cr_assert_eq(public_key_to_x25519(&public, converted_public), ASCIICHAT_OK);
    cr_assert_eq(private_key_to_x25519(&secret, converted_secret), ASCIICHAT_OK);
    cr_assert_eq(crypto_scalarmult_base(derived_public, converted_secret), 0);
    cr_assert_arr_eq(converted_public, derived_public, sizeof(derived_public));
    sodium_memzero(&secret, sizeof(secret));
    sodium_memzero(converted_secret, sizeof(converted_secret));
    if (index) {
      memset(&secret, 0xa5, sizeof(secret));
      cr_assert_eq(parse_ssh_private_key(private_path, &secret), ASCIICHAT_OK);
      cr_assert(secret.use_ssh_agent);
      cr_assert_not(secret.use_gpg_agent);
      cr_assert_eq(ed25519_sign_message(&secret, message, sizeof(message), signature), ASCIICHAT_OK);
      cr_assert_eq(crypto_sign_verify_detached(signature, message, sizeof(message), public.key), 0);
      sodium_memzero(&secret, sizeof(secret));
      platform_setenv("SSH_AUTH_SOCK", directory);
      platform_setenv("ASCII_CHAT_KEY_PASSWORD", "wrong-passphrase");
      cr_assert_eq(parse_ssh_private_key(private_path, &secret), ERROR_CRYPTO_KEY);
      sodium_memzero(&secret, sizeof(secret));
      platform_setenv("SSH_AUTH_SOCK", socket_path);
    }
  }
}
