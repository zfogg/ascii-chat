#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/util/path.h>
#include <libplatform/filesystem.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
TEST_CASE("path_policy/normalized_containment_checks_component_boundaries_and_traversal", "[path_policy] [unit]") {
#ifndef _WIN32
  char output[256];
  REQUIRE(path_normalize_copy("/tmp/base/./inner/../file", output, sizeof(output)));
  REQUIRE(std::strcmp((output), ("/tmp/base/file")) == 0);
  REQUIRE(path_is_within_base("/tmp/base/inner/../file", "/tmp/base"));
  REQUIRE(path_is_within_base("/tmp/base", "/tmp/base/"));
  REQUIRE(!(path_is_within_base("/tmp/base-other/file", "/tmp/base")));
  REQUIRE(!(path_is_within_base("/tmp/base/../outside", "/tmp/base")));
  REQUIRE(!(path_is_within_base("relative/file", "/tmp/base")));
  const char *bases[] = {"/opt/example", "/tmp/base"};
  REQUIRE(path_is_within_any_base("/tmp/base/file", bases, 2));
  REQUIRE(!(path_is_within_any_base("/tmp/other", bases, 2)));
  REQUIRE(!(path_normalize_copy("/too-long", output, 2)));
#endif
  REQUIRE(path_looks_like_path("./key.pub"));
  REQUIRE(path_looks_like_path("~/key.pub"));
  REQUIRE(!(path_looks_like_path("github:zfogg")));
  REQUIRE(!(path_looks_like_path("0123456789abcdef")));
  REQUIRE(!(path_looks_like_path(NULL)));
}

TEST_CASE("path_policy/log_validation_protects_existing_unrelated_files_without_modifying_them", "[path_policy] [unit]") {
  char path[256];
  int fd;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "ascii-path-policy", &fd)) == (0));
  char *normalized = NULL;
  REQUIRE((path_validate_user_path(path, PATH_ROLE_LOG_FILE, &normalized)) == (ASCIICHAT_OK));
  REQUIRE((normalized) != nullptr);
  SAFE_FREE(normalized);
  const char contents[] = "unrelated data that must survive validation\n";
  REQUIRE((platform_write(fd, contents, sizeof(contents) - 1)) == ((ssize_t)sizeof(contents) - 1));
  platform_close(fd);
  REQUIRE((path_validate_user_path(path, PATH_ROLE_LOG_FILE, &normalized)) == (ERROR_LOGGING_INIT));
  REQUIRE((normalized) == nullptr);
  FILE *file = platform_fopen("verify-path-fixture", path, "rb");
  REQUIRE((file) != nullptr);
  char actual[sizeof(contents)] = {0};
  REQUIRE((fread(actual, 1, sizeof(contents) - 1, file)) == (sizeof(contents) - 1));
  fclose(file);
  REQUIRE(std::strcmp((actual), (contents)) == 0);
  platform_delete_temp_file(path);
}

TEST_CASE("path_policy/relative_key_paths_are_canonical_and_protected_system_paths_are_rejected", "[path_policy] [unit]") {
  char *normalized = NULL;
  REQUIRE((path_validate_user_path("./keys/../key.pub", PATH_ROLE_KEY_PUBLIC, &normalized)) == (ASCIICHAT_OK));
  REQUIRE((normalized) != nullptr);
  REQUIRE(path_is_absolute(normalized));
  REQUIRE((strstr(normalized, "/../")) == nullptr);
  REQUIRE((strstr(normalized, "key.pub")) != nullptr);
  SAFE_FREE(normalized);
#ifndef _WIN32
  REQUIRE((path_validate_user_path("/etc/passwd", PATH_ROLE_KEY_PUBLIC, &normalized)) != (ASCIICHAT_OK));
  REQUIRE((normalized) == nullptr);
  REQUIRE((path_validate_user_path("/etc/shadow", PATH_ROLE_LOG_FILE, &normalized)) != (ASCIICHAT_OK));
  REQUIRE((normalized) == nullptr);
#endif
  REQUIRE((path_validate_user_path("", PATH_ROLE_KEY_PRIVATE, &normalized)) != (ASCIICHAT_OK));
  REQUIRE((normalized) == nullptr);
}
