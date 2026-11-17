#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/crypto/discovery_keys.h>
#include <ascii-chat/crypto/keys.h>
#include <libplatform/filesystem.h>
#include <libplatform/system.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/tests/logging.h>
#include <sodium.h>
#include <string.h>

static char root[PLATFORM_MAX_PATH_LENGTH];
static char *old_config;

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  const char *config = getenv("XDG_CONFIG_HOME");
  old_config = NULL;
  if (config)
    SAFE_STRDUP(old_config, config);
  REQUIRE((platform_mkdtemp(root, sizeof(root), "discovery-cache")) == (ASCIICHAT_OK));
  REQUIRE((platform_setenv("XDG_CONFIG_HOME", root)) == (0));
}

static void teardown(void) {
  if (old_config) {
    platform_setenv("XDG_CONFIG_HOME", old_config);
    SAFE_FREE(old_config);
  } else {
    platform_unsetenv("XDG_CONFIG_HOME");
  }
  platform_rmdir_recursive(root);
  log_destroy();
}
static void prepare_parent(const char *server, char *path) {
  REQUIRE((discovery_keys_get_cache_path(server, path, PLATFORM_MAX_PATH_LENGTH)) == (ASCIICHAT_OK));
  char parent[PLATFORM_MAX_PATH_LENGTH];
  SAFE_STRNCPY(parent, path, sizeof(parent));
  char *slash = strrchr(parent, '/');
  REQUIRE((slash) != nullptr);
  *slash = '\0';
  REQUIRE((platform_mkdir_recursive(parent, 0700)) == (ASCIICHAT_OK));
}

TEST_CASE("discovery_cache/saved_key_is_standard_openssh_and_roundtrips", "[discovery_cache]") {
  uint8_t key[32], loaded[32];
  for (size_t i = 0; i < sizeof(key); ++i)
    key[i] = (uint8_t)(i * 7 + 1);
  char path[PLATFORM_MAX_PATH_LENGTH];
  prepare_parent("cache.example", path);
  REQUIRE((discovery_keys_save_cached("cache.example", key)) == (ASCIICHAT_OK));
  REQUIRE((discovery_keys_load_cached("cache.example", loaded)) == (ASCIICHAT_OK));
  REQUIRE(std::memcmp((loaded), (key), (sizeof(key))) == 0);

  FILE *file = platform_fopen("cache-test", path, "r");
  REQUIRE((file) != nullptr);
  char algorithm[32], encoded[128];
  REQUIRE((fscanf(file, "%31s %127s", algorithm, encoded)) == (2));
  fclose(file);
  REQUIRE(std::strcmp((algorithm), ("ssh-ed25519")) == 0);
  uint8_t wire[64];
  size_t length = 0;
  REQUIRE(((sodium_base642bin(wire, sizeof(wire), encoded, strlen(encoded), NULL, &length, NULL, sodium_base64_VARIANT_ORIGINAL))) == (0));
  const uint8_t prefix[] = {0, 0, 0, 11, 's', 's', 'h', '-', 'e', 'd', '2', '5', '5', '1', '9', 0, 0, 0, 32};
  REQUIRE((length) == (sizeof(prefix) + sizeof(key)));
  REQUIRE(std::memcmp((wire), (prefix), (sizeof(prefix))) == 0);
  REQUIRE(std::memcmp((wire + sizeof(prefix)), (key), (sizeof(key))) == 0);
  public_key_t parsed;
  REQUIRE((parse_public_key(path, &parsed)) == (ASCIICHAT_OK));
  REQUIRE(std::memcmp((parsed.key), (key), (sizeof(key))) == 0);
  REQUIRE((discovery_keys_clear_cache("cache.example")) == (ASCIICHAT_OK));
  REQUIRE((discovery_keys_load_cached("cache.example", loaded)) == (ERROR_FILE_NOT_FOUND));
  REQUIRE((discovery_keys_clear_cache("cache.example")) == (ASCIICHAT_OK));
}

TEST_CASE("discovery_cache/first_use_creates_all_cache_directories", "[discovery_cache]") {
  uint8_t key[32] = {1}, loaded[32];
  REQUIRE((discovery_keys_save_cached("new.example", key)) == (ASCIICHAT_OK));
  REQUIRE((discovery_keys_load_cached("new.example", loaded)) == (ASCIICHAT_OK));
  REQUIRE(std::memcmp((loaded), (key), (sizeof(key))) == 0);
}

TEST_CASE("discovery_cache/explicit_key_rotation_requires_acceptance_and_preserves_rejected_key", "[discovery_cache]") {
  uint8_t first[32] = {1}, second[32] = {2}, loaded[32];
  char first_path[PLATFORM_MAX_PATH_LENGTH], second_path[PLATFORM_MAX_PATH_LENGTH];
  REQUIRE((discovery_keys_save_cached("source-one.example", first)) == (ASCIICHAT_OK));
  REQUIRE((discovery_keys_save_cached("source-two.example", second)) == (ASCIICHAT_OK));
  REQUIRE((discovery_keys_get_cache_path("source-one.example", first_path, sizeof(first_path))) == (ASCIICHAT_OK));
  REQUIRE((discovery_keys_get_cache_path("source-two.example", second_path, sizeof(second_path))) == (ASCIICHAT_OK));
  REQUIRE((discovery_keys_verify("target.example", first_path, loaded)) == (ASCIICHAT_OK));
  REQUIRE(std::memcmp((loaded), (first), (sizeof(first))) == 0);
  REQUIRE((discovery_keys_verify("target.example", first_path, loaded)) == (ASCIICHAT_OK));
  REQUIRE(std::memcmp((loaded), (first), (sizeof(first))) == 0);

  platform_setenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", "n");
  memset(loaded, 0xa5, sizeof(loaded));
  REQUIRE((discovery_keys_verify("target.example", second_path, loaded)) == (ERROR_CRYPTO_VERIFICATION));
  for (size_t i = 0; i < sizeof(loaded); ++i)
    REQUIRE((loaded[i]) == (0xa5));
  REQUIRE((discovery_keys_load_cached("target.example", loaded)) == (ASCIICHAT_OK));
  REQUIRE(std::memcmp((loaded), (first), (sizeof(first))) == 0);

  platform_setenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", "y");
  REQUIRE((discovery_keys_verify("target.example", second_path, loaded)) == (ASCIICHAT_OK));
  REQUIRE(std::memcmp((loaded), (second), (sizeof(second))) == 0);
  REQUIRE((discovery_keys_load_cached("target.example", loaded)) == (ASCIICHAT_OK));
  REQUIRE(std::memcmp((loaded), (second), (sizeof(second))) == 0);
  platform_unsetenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
}

TEST_CASE("discovery_cache/rejects_missing_and_malformed_keys_without_publishing_output", "[discovery_cache]") {
  uint8_t loaded[32];
  memset(loaded, 0xa5, sizeof(loaded));
  char path[PLATFORM_MAX_PATH_LENGTH];
  prepare_parent("broken.example", path);
  REQUIRE((discovery_keys_load_cached("broken.example", loaded)) == (ERROR_FILE_NOT_FOUND));
  FILE *file = platform_fopen("broken-cache-test", path, "w");
  REQUIRE((file) != nullptr);
  fputs("ssh-ed25519 not-a-key\n", file);
  fclose(file);
  REQUIRE((discovery_keys_load_cached("broken.example", loaded)) == (ERROR_CRYPTO_KEY));
  REQUIRE((discovery_keys_load_file(path, loaded)) == (ERROR_CRYPTO_KEY));
  REQUIRE((discovery_keys_verify("third-party.example", NULL, loaded)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_verify("third-party.example", path, loaded)) == (ERROR_CRYPTO_KEY));
  for (size_t i = 0; i < sizeof(loaded); ++i)
    REQUIRE((loaded[i]) == (0xa5));
}

TEST_CASE("discovery_cache/invalid_parameters", "[discovery_cache]") {
  uint8_t key[32] = {0};
  char path[128];
  REQUIRE((discovery_keys_get_cache_path(NULL, path, sizeof(path))) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_get_cache_path("server", NULL, sizeof(path))) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_get_cache_path("server", path, 0)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_save_cached(NULL, key)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_save_cached("server", NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_load_cached(NULL, key)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_load_cached("server", NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_load_file(NULL, key)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_load_file("file", NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_download_https(NULL, key)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_download_https("https://unused.invalid/key", NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_verify(NULL, NULL, key)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_verify("server", NULL, NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_verify_change(NULL, key, key)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_verify_change("server", NULL, key)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_verify_change("server", key, NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE((discovery_keys_clear_cache(NULL)) == (ERROR_INVALID_PARAM));
}
