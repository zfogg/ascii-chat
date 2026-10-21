#include <criterion/criterion.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

typedef struct {
  uintptr_t key;
  int visits;
  uint64_t generation;
} observation_t;

static void visit(uintptr_t key, uint64_t generation, const char *name, const char *type, const char *file, int line,
                  void *data) {
  (void)name;
  (void)type;
  (void)file;
  (void)line;
  observation_t *seen = data;
  if (key == seen->key) {
    ++seen->visits;
    seen->generation = generation;
  }
}

static void read_value(uintptr_t key, void *data) {
  *(int *)data = *(int *)key;
}

TestSuite(registry_snapshot);

Test(registry_snapshot, metadata_and_protected_snapshots_follow_rename_and_removal) {
  cr_assert_eq(named_init(), ASCIICHAT_OK);
  int object = 42, value = 0;
  uintptr_t key = (uintptr_t)&object;
  cr_assert_str_eq(named_register_fmt(key, "fixture", "%p", __FILE__, __LINE__, __func__, "source.%d", 17),
                   "source.17");
  named_metadata_t before;
  cr_assert(named_metadata_copy(key, &before));
  cr_assert_str_eq(before.name, "source.17");
  cr_assert_str_eq(before.type, "fixture");
  cr_assert_str_eq(named_get_type(key), "fixture");
  cr_assert_str_eq(named_get_format_spec(key), "%p");
  cr_assert(named_registry_has_type("fixture", strlen("fixture")));
  cr_assert(named_registry_read(key, "fixture", read_value, &value));
  cr_assert_eq(value, 42);
  cr_assert_not(named_registry_read(key, "other", read_value, &value));
  observation_t seen = {.key = key};
  size_t required = 0;
  bool complete = true;
  cr_assert_eq(named_registry_try_snapshot(0, &required, &complete, visit, &seen), ASCIICHAT_OK);
  cr_assert_not(complete);
  cr_assert_gt(required, 0);
  cr_assert_eq(seen.visits, 0);
  cr_assert_eq(named_registry_try_snapshot(required, &required, &complete, visit, &seen), ASCIICHAT_OK);
  cr_assert(complete);
  cr_assert_eq(seen.visits, 1);
  cr_assert_gt(seen.generation, 0);
#ifdef NDEBUG
  cr_assert_null(named_update_name(key, "renamed"));
#else
  cr_assert_not_null(named_update_name(key, "renamed"));
#endif
  named_metadata_t after;
  cr_assert(named_metadata_copy(key, &after));
#ifdef NDEBUG
  cr_assert_str_eq(after.name, "source.17");
#else
  cr_assert_not_null(strstr(after.name, "renamed"));
#endif
  cr_assert_str_eq(before.name, "source.17");
  named_unregister(key);
  cr_assert_not(named_metadata_copy(key, &after));
  cr_assert_not(named_registry_read(key, "fixture", read_value, &value));
  cr_assert_null(named_get(key));
  cr_assert_eq(named_registry_try_snapshot(0, NULL, &complete, visit, &seen), ERROR_INVALID_PARAM);
}
