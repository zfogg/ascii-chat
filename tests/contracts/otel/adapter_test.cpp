/* The external trace fixture also uses these cases to produce protobuf bytes. */
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#define CHECK(value) ASCIICHAT_REQUIRE(value)
#define main otel_contract_fixture_main
#include "../../standalone/media/otel_contract_test.c"
#undef main
#undef CHECK

static void setup(void) {
  log_init(NULL, LOG_ERROR, false, false);
}
TEST_CASE("otel_adapter/bounded_sampling", "[otel_adapter]") { bounded_sampling(); }
TEST_CASE("otel_adapter/dropped_errors", "[otel_adapter]") { dropped_errors(); }
TEST_CASE("otel_adapter/retained_budget", "[otel_adapter]") { retained_budget(); }
TEST_CASE("otel_adapter/sqlite_prepare", "[otel_adapter]") { sqlite_prepare_contract(); }
TEST_CASE("otel_adapter/query_catalog", "[otel_adapter]") { catalog_contract(); }
