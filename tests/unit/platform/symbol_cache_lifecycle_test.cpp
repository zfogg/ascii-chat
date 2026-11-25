#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/common.h>
#include <libplatform/symbols.h>

#include <ascii-chat/tests/logging.h>
#include <string.h>
#include <libplatform/filesystem.h>
#include <libplatform/system.h>
#include <ascii-chat/util/pcre2.h>
#include <stdlib.h>

static const char *const symbol_cache_test_libraries[] = {"libasciichat", NULL};

#ifndef _WIN32
static char fallback_directory[PLATFORM_MAX_PATH_LENGTH];
static char *fallback_old_env[3];
static const char *fallback_env_names[] = {"PATH", "LLVM_SYMBOLIZER_PATH", "ADDR2LINE_PATH"};
static void fallback_cleanup(void) {
  symbol_cache_destroy();
  for (size_t i = 0; i < 3; ++i) {
    if (fallback_old_env[i]) {
      platform_setenv(fallback_env_names[i], fallback_old_env[i]);
      SAFE_FREE(fallback_old_env[i]);
    } else {
      platform_unsetenv(fallback_env_names[i]);
    }
  }
  if (fallback_directory[0])
    platform_rmdir_recursive(fallback_directory);
  log_destroy();
}
static void fallback_setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  for (size_t i = 0; i < 3; ++i) {
    const char *value = getenv(fallback_env_names[i]);
    if (value)
      SAFE_STRDUP(fallback_old_env[i], value);
  }
  REQUIRE((platform_mkdtemp(fallback_directory, sizeof(fallback_directory), "symbol-fallback")) == (ASCIICHAT_OK));
  char script[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(script, sizeof(script), "%s/addr2line", fallback_directory);
  FILE *file = platform_fopen("addr2line-fixture", script, "w");
  REQUIRE((file) != nullptr);
  const char *program = "#!/bin/sh\n"
                        "for argument do\n"
                        " case \"$argument\" in\n"
                        "  0x1111) printf 'both_known\\n/tmp/project/source.c:42\\n';;\n"
                        "  0x2222) printf 'function_only\\n??:0\\n';;\n"
                        "  0x3333) printf '??\\n/tmp/project/source.c:73\\n';;\n"
                        "  0x4444) printf '??\\n??:?\\n';;\n"
                        " esac\n"
                        "done\n";
  REQUIRE((fwrite(program, 1, strlen(program), file)) == (strlen(program)));
  REQUIRE((fclose(file)) == (0));
  REQUIRE((platform_chmod(script, 0700)) == (0));
  REQUIRE((platform_setenv("PATH", fallback_directory)) == (0));
  REQUIRE((platform_setenv("LLVM_SYMBOLIZER_PATH", "/missing/llvm-symbolizer")) == (0));
  REQUIRE((platform_setenv("ADDR2LINE_PATH", script)) == (0));
}
TEST_CASE("symbol_cache_fallback/addr2line_handles_mixed_resolution_and_caches_owned_results", "[symbol_cache_fallback] [unit]") {
  REQUIRE((symbol_cache_init("ascii-chat", symbol_cache_test_libraries)) == (ASCIICHAT_OK));
  void *addresses[] = {(void *)0x1111, (void *)0x2222, (void *)0x3333, (void *)0x4444};
  char **symbols = symbol_cache_resolve_batch(addresses, 4, NULL);
  REQUIRE((symbols) != nullptr);
  for (int i = 0; i < 4; ++i)
    REQUIRE((symbols[i]) != nullptr);
  REQUIRE((strstr(symbols[0], "source.c:42")) != nullptr);
  REQUIRE((strstr(symbols[0], "both_known()")) != nullptr);
  REQUIRE((strstr(symbols[1], "function_only() at 0x2222")) != nullptr);
  REQUIRE((strstr(symbols[2], "source.c:73")) != nullptr);
  REQUIRE((strstr(symbols[2], "unknown function")) != nullptr);
  REQUIRE(std::strcmp((symbols[3]), ("0x4444")) == 0);
  char **cached = symbol_cache_resolve_batch(addresses, 4, NULL);
  REQUIRE((cached) != nullptr);
  for (int i = 0; i < 4; ++i)
    REQUIRE(std::strcmp((cached[i]), (symbols[i])) == 0);
  symbol_cache_free_symbols(cached);
  symbol_cache_free_symbols(symbols);
}
#endif

TEST_CASE("symbol_cache_lifecycle/batch_resolution_identifies_exported_functions_and_reuses_the_cache", "[symbol_cache_lifecycle] [unit]") {
#ifdef _WIN32
  SUCCEED("Windows resolves native symbols through the platform debugger API rather than the POSIX symbolizer names tested here");
  return;
#endif
  REQUIRE((symbol_cache_init("ascii-chat", symbol_cache_test_libraries)) == (ASCIICHAT_OK));
  void *addresses[] = {(void *)symbol_cache_init, (void *)symbol_cache_destroy, (void *)symbol_cache_insert};
  char **first = symbol_cache_resolve_batch(addresses, 3, NULL);
  REQUIRE((first) != nullptr);
  REQUIRE((strstr(first[0], "symbol_cache_init")) != nullptr);
  REQUIRE((strstr(first[1], "symbol_cache_destroy")) != nullptr);
  char **second = symbol_cache_resolve_batch(addresses, 3, NULL);
  REQUIRE((second) != nullptr);
  for (int i = 0; i < 3; ++i)
    REQUIRE(std::strcmp((first[i]), (second[i])) == 0);
  symbol_cache_free_symbols(first);
  symbol_cache_free_symbols(second);
  REQUIRE((symbol_cache_resolve_batch(NULL, 0, NULL)) == nullptr);
  symbol_cache_destroy();
}

TEST_CASE("symbol_cache_lifecycle/cached_names_are_copied_and_updates_do_not_invalidate_previous_results", "[symbol_cache_lifecycle] [unit]") {
  REQUIRE((symbol_cache_init("ascii-chat", symbol_cache_test_libraries)) == (ASCIICHAT_OK));
  int key, absent;
  REQUIRE((symbol_cache_lookup(&absent)) == nullptr);
  char name[] = "original_symbol";
  REQUIRE(symbol_cache_insert(&key, name));
  name[0] = 'X';
  char *before = (char *)symbol_cache_lookup(&key);
  REQUIRE((before) != nullptr);
  REQUIRE(std::strcmp((before), ("original_symbol")) == 0);
  REQUIRE(symbol_cache_insert(&key, "updated_symbol"));
  char *after = (char *)symbol_cache_lookup(&key);
  REQUIRE(std::strcmp((after), ("updated_symbol")) == 0);
  REQUIRE(std::strcmp((before), ("original_symbol")) == 0);
  uint64_t hits, misses;
  size_t entries;
  symbol_cache_get_stats(&hits, &misses, &entries);
  REQUIRE((hits) >= (2));
  REQUIRE((misses) >= (1));
  REQUIRE((entries) >= (1));
  REQUIRE(!(symbol_cache_insert(NULL, "invalid")));
  REQUIRE(!(symbol_cache_insert(&key, NULL)));
  // symbol_cache_lookup returns duplicated strings owned by the caller. The
  // platform layer uses malloc for these results, so release them directly.
  free(before);
  free(after);
  symbol_cache_destroy();
  REQUIRE((symbol_cache_lookup(&key)) == nullptr);
}

TEST_CASE("symbol_cache_lifecycle/source_paths_classify_project_external_and_truncation_without_partial_writes", "[symbol_cache_lifecycle] [unit]") {
  const char *const libraries[] = {"libasciichat.so", "asciichat.dll", "libasciichat.dylib", NULL};
  const char *project[] = {"ascii-chat", "C:\\tools\\ASCII-CHAT.EXE", "libasciichat.so.0.1.0"};
  for (size_t i = 0; i < 3; ++i)
    REQUIRE(symbol_module_is_project(project[i], "ascii-chat", libraries));
  REQUIRE(!(symbol_module_is_project("libc.so.6", "ascii-chat", libraries)));
  REQUIRE(!(symbol_module_is_project(NULL, "ascii-chat", libraries)));
  char output[256];
  bool external;
  REQUIRE(symbol_format_source_path("libasciichat.so", "ascii-chat", libraries, "lib/audio/audio.c", output, sizeof(output), &external, NULL) == 0);
  REQUIRE(!(external));
  REQUIRE(std::strcmp((output), ("lib/audio/audio.c")) == 0);
  REQUIRE(symbol_format_source_path("libc.so", "ascii-chat", libraries, "/a/b/c/d/e/f/source.c", output, sizeof(output), &external, NULL) == 0);
  REQUIRE(external);
  REQUIRE((strstr(output, "c/d/e/f/source.c")) != nullptr);
  strcpy(output, "unchanged");
  REQUIRE(symbol_format_source_path("libc.so", "ascii-chat", libraries, "/a/b/source.c", output, 2, &external, NULL) != 0);
  REQUIRE(std::strcmp((output), ("unchanged")) == 0);
}
