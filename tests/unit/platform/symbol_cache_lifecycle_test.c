#include <criterion/criterion.h>
#include <ascii-chat/platform/symbols.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/util/pcre2.h>
#include <stdlib.h>

TestSuite(symbol_cache_lifecycle);

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
  asciichat_pcre2_cleanup_all();
}
static void fallback_setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  for (size_t i = 0; i < 3; ++i) {
    const char *value = getenv(fallback_env_names[i]);
    if (value)
      SAFE_STRDUP(fallback_old_env[i], value);
  }
  cr_assert_eq(platform_mkdtemp(fallback_directory, sizeof(fallback_directory), "symbol-fallback"), ASCIICHAT_OK);
  char script[PLATFORM_MAX_PATH_LENGTH];
  safe_snprintf(script, sizeof(script), "%s/addr2line", fallback_directory);
  FILE *file = platform_fopen("addr2line-fixture", script, "w");
  cr_assert_not_null(file);
  const char *program = "#!/bin/sh\n"
      "for argument do\n"
      " case \"$argument\" in\n"
      "  0x1111) printf 'both_known\\n/tmp/project/source.c:42\\n';;\n"
      "  0x2222) printf 'function_only\\n??:0\\n';;\n"
      "  0x3333) printf '??\\n/tmp/project/source.c:73\\n';;\n"
      "  0x4444) printf '??\\n??:?\\n';;\n"
      " esac\n"
      "done\n";
  cr_assert_eq(fwrite(program, 1, strlen(program), file), strlen(program));
  cr_assert_eq(fclose(file), 0);
  cr_assert_eq(platform_chmod(script, 0700), 0);
  cr_assert_eq(platform_setenv("PATH", fallback_directory), 0);
  cr_assert_eq(platform_setenv("LLVM_SYMBOLIZER_PATH", "/missing/llvm-symbolizer"), 0);
  cr_assert_eq(platform_setenv("ADDR2LINE_PATH", script), 0);
}
Test(symbol_cache_fallback, addr2line_handles_mixed_resolution_and_caches_owned_results,
     .init = fallback_setup, .fini = fallback_cleanup) {
  cr_assert_eq(symbol_cache_init(), ASCIICHAT_OK);
  void *addresses[] = {(void *)0x1111, (void *)0x2222, (void *)0x3333, (void *)0x4444};
  char **symbols = symbol_cache_resolve_batch(addresses, 4);
  cr_assert_not_null(symbols);
  for (int i = 0; i < 4; ++i)
    cr_assert_not_null(symbols[i]);
  cr_assert_not_null(strstr(symbols[0], "source.c:42"));
  cr_assert_not_null(strstr(symbols[0], "both_known()"));
  cr_assert_not_null(strstr(symbols[1], "function_only() at 0x2222"));
  cr_assert_not_null(strstr(symbols[2], "source.c:73"));
  cr_assert_not_null(strstr(symbols[2], "unknown function"));
  cr_assert_str_eq(symbols[3], "0x4444");
  char **cached = symbol_cache_resolve_batch(addresses, 4);
  cr_assert_not_null(cached);
  for (int i = 0; i < 4; ++i)
    cr_assert_str_eq(cached[i], symbols[i]);
  symbol_cache_free_symbols(cached);
  symbol_cache_free_symbols(symbols);
}
#endif

Test(symbol_cache_lifecycle, batch_resolution_identifies_exported_functions_and_reuses_the_cache) {
  cr_assert_eq(symbol_cache_init(), ASCIICHAT_OK);
  void *addresses[] = {(void *)symbol_cache_init, (void *)symbol_cache_destroy, (void *)symbol_cache_insert};
  char **first = symbol_cache_resolve_batch(addresses, 3);
  cr_assert_not_null(first);
  cr_assert_not_null(strstr(first[0], "symbol_cache_init"), "Resolved: %s", first[0]);
  cr_assert_not_null(strstr(first[1], "symbol_cache_destroy"), "Resolved: %s", first[1]);
  char **second = symbol_cache_resolve_batch(addresses, 3);
  cr_assert_not_null(second);
  for (int i = 0; i < 3; ++i)
    cr_assert_str_eq(first[i], second[i]);
  symbol_cache_free_symbols(first);
  symbol_cache_free_symbols(second);
  cr_assert_null(symbol_cache_resolve_batch(NULL, 0));
  symbol_cache_destroy();
}

Test(symbol_cache_lifecycle, cached_names_are_copied_and_updates_do_not_invalidate_previous_results) {
  cr_assert_eq(symbol_cache_init(), ASCIICHAT_OK);
  int key, absent;
  cr_assert_null(symbol_cache_lookup(&absent));
  char name[] = "original_symbol";
  cr_assert(symbol_cache_insert(&key, name));
  name[0] = 'X';
  char *before = (char *)symbol_cache_lookup(&key);
  cr_assert_not_null(before);
  cr_assert_str_eq(before, "original_symbol");
  cr_assert(symbol_cache_insert(&key, "updated_symbol"));
  char *after = (char *)symbol_cache_lookup(&key);
  cr_assert_str_eq(after, "updated_symbol");
  cr_assert_str_eq(before, "original_symbol");
  uint64_t hits, misses;
  size_t entries;
  symbol_cache_get_stats(&hits, &misses, &entries);
  cr_assert_geq(hits, 2);
  cr_assert_geq(misses, 1);
  cr_assert_geq(entries, 1);
  cr_assert_not(symbol_cache_insert(NULL, "invalid"));
  cr_assert_not(symbol_cache_insert(&key, NULL));
  SAFE_FREE(before);
  SAFE_FREE(after);
  symbol_cache_destroy();
  cr_assert_null(symbol_cache_lookup(&key));
}

Test(symbol_cache_lifecycle, source_paths_classify_project_external_and_truncation_without_partial_writes) {
  const char *project[] = {"ascii-chat", "C:\\tools\\ASCII-CHAT.EXE", "libasciichat.so.0.1.0"};
  for (size_t i = 0; i < 3; ++i)
    cr_assert(symbol_module_is_project(project[i]));
  cr_assert_not(symbol_module_is_project("libc.so.6"));
  cr_assert_not(symbol_module_is_project(NULL));
  char output[256];
  bool external;
  cr_assert_eq(symbol_format_source_path("libasciichat.so", "lib/audio/audio.c", output, sizeof(output), &external),
               ASCIICHAT_OK);
  cr_assert_not(external);
  cr_assert_str_eq(output, "lib/audio/audio.c");
  cr_assert_eq(symbol_format_source_path("libc.so", "/a/b/c/d/e/f/source.c", output, sizeof(output), &external),
               ASCIICHAT_OK);
  cr_assert(external);
  cr_assert_not_null(strstr(output, "c/d/e/f/source.c"));
  strcpy(output, "unchanged");
  cr_assert_eq(symbol_format_source_path("libc.so", "/a/b/source.c", output, 2, &external), ERROR_BUFFER_OVERFLOW);
  cr_assert_str_eq(output, "unchanged");
}
