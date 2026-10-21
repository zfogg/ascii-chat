#include <criterion/criterion.h>
#include <ascii-chat/platform/symbols.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

TestSuite(symbol_cache_lifecycle);

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
