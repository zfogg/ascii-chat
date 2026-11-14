#include <ascii-chat/tests/catch2_test_helpers.hpp>
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
  observation_t *seen = static_cast<observation_t *>(data);
  if (key == seen->key) {
    ++seen->visits;
    seen->generation = generation;
  }
}

static void read_value(uintptr_t key, void *data) {
  *(int *)data = *(int *)key;
}
TEST_CASE("registry_snapshot/metadata_and_protected_snapshots_follow_rename_and_removal", "[registry_snapshot]") {
  ASCIICHAT_REQUIRE_EQ(named_init(), ASCIICHAT_OK);
  int object = 42, value = 0;
  uintptr_t key = (uintptr_t)&object;
  ASCIICHAT_REQUIRE_STR_EQ(named_register_fmt(key, "fixture", "%p", __FILE__, __LINE__, __func__, "source.%d", 17),
                   "source.17");
  named_metadata_t before;
  ASCIICHAT_REQUIRE(named_metadata_copy(key, &before));
  ASCIICHAT_REQUIRE_STR_EQ(before.name, "source.17");
  ASCIICHAT_REQUIRE_STR_EQ(before.type, "fixture");
  ASCIICHAT_REQUIRE_STR_EQ(named_get_type(key), "fixture");
  ASCIICHAT_REQUIRE_STR_EQ(named_get_format_spec(key), "%p");
  ASCIICHAT_REQUIRE(named_registry_has_type("fixture", strlen("fixture")));
  ASCIICHAT_REQUIRE(named_registry_read(key, "fixture", read_value, &value));
  ASCIICHAT_REQUIRE_EQ(value, 42);
  ASCIICHAT_REQUIRE_NOT(named_registry_read(key, "other", read_value, &value));
  observation_t seen = {.key = key};
  size_t required = 0;
  bool complete = true;
  ASCIICHAT_REQUIRE_EQ(named_registry_try_snapshot(0, &required, &complete, visit, &seen), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(complete);
  ASCIICHAT_REQUIRE_GT(required, 0);
  ASCIICHAT_REQUIRE_EQ(seen.visits, 0);
  ASCIICHAT_REQUIRE_EQ(named_registry_try_snapshot(required, &required, &complete, visit, &seen), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(complete);
  ASCIICHAT_REQUIRE_EQ(seen.visits, 1);
  ASCIICHAT_REQUIRE_GT(seen.generation, 0);
#ifdef NDEBUG
  ASCIICHAT_REQUIRE_NULL(named_update_name(key, "renamed"));
#else
  ASCIICHAT_REQUIRE_NOT_NULL(named_update_name(key, "renamed"));
#endif
  named_metadata_t after;
  ASCIICHAT_REQUIRE(named_metadata_copy(key, &after));
#ifdef NDEBUG
  ASCIICHAT_REQUIRE_STR_EQ(after.name, "source.17");
#else
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(after.name, "renamed"));
#endif
  ASCIICHAT_REQUIRE_STR_EQ(before.name, "source.17");
  named_unregister(key);
  ASCIICHAT_REQUIRE_NOT(named_metadata_copy(key, &after));
  ASCIICHAT_REQUIRE_NOT(named_registry_read(key, "fixture", read_value, &value));
  ASCIICHAT_REQUIRE_NULL(named_get(key));
  ASCIICHAT_REQUIRE_EQ(named_registry_try_snapshot(0, NULL, &complete, visit, &seen), ERROR_INVALID_PARAM);
}
