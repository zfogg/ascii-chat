#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

typedef struct {
  uintptr_t key;
  int visits;
  uint64_t generation;
} observation_t;

static void visit(uintptr_t key, uint64_t generation, const char *name, const char *type, const char *file, int line, void *data) {
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
  REQUIRE((named_init()) == (ASCIICHAT_OK));
  int object = 42, value = 0;
  uintptr_t key = (uintptr_t)&object;
  REQUIRE(std::strcmp((named_register_fmt(key, "fixture", "%p", __FILE__, __LINE__, __func__, "source.%d", 17)), ("source.17")) == 0);
  named_metadata_t before;
  REQUIRE(named_metadata_copy(key, &before));
  REQUIRE(std::strcmp((before.name), ("source.17")) == 0);
  REQUIRE(std::strcmp((before.type), ("fixture")) == 0);
  REQUIRE(std::strcmp((named_get_type(key)), ("fixture")) == 0);
  REQUIRE(std::strcmp((named_get_format_spec(key)), ("%p")) == 0);
  REQUIRE(named_registry_has_type("fixture", strlen("fixture")));
  REQUIRE(named_registry_read(key, "fixture", read_value, &value));
  REQUIRE((value) == (42));
  REQUIRE(!(named_registry_read(key, "other", read_value, &value)));
  observation_t seen = {.key = key};
  size_t required = 0;
  bool complete = true;
  REQUIRE((named_registry_try_snapshot(0, &required, &complete, visit, &seen)) == (ASCIICHAT_OK));
  REQUIRE(!(complete));
  REQUIRE((required) > (0));
  REQUIRE((seen.visits) == (0));
  REQUIRE((named_registry_try_snapshot(required, &required, &complete, visit, &seen)) == (ASCIICHAT_OK));
  REQUIRE(complete);
  REQUIRE((seen.visits) == (1));
  REQUIRE((seen.generation) > (0));
#ifdef NDEBUG
  REQUIRE((named_update_name(key, "renamed")) == nullptr);
#else
  REQUIRE((named_update_name(key, "renamed")) != nullptr);
#endif
  named_metadata_t after;
  REQUIRE(named_metadata_copy(key, &after));
#ifdef NDEBUG
  REQUIRE(std::strcmp((after.name), ("source.17")) == 0);
#else
  REQUIRE((strstr(after.name, "renamed")) != nullptr);
#endif
  REQUIRE(std::strcmp((before.name), ("source.17")) == 0);
  named_unregister(key);
  REQUIRE(!(named_metadata_copy(key, &after)));
  REQUIRE(!(named_registry_read(key, "fixture", read_value, &value)));
  REQUIRE((named_get(key)) == nullptr);
  REQUIRE((named_registry_try_snapshot(0, NULL, &complete, visit, &seen)) == (ERROR_INVALID_PARAM));
}
