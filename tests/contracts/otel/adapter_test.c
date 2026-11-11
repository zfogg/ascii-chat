/* The external trace fixture also uses these cases to produce protobuf bytes. */
#include <criterion/criterion.h>
#define CHECK(value) cr_assert(value)
#define main otel_contract_fixture_main
#include "../../standalone/media/otel_contract_test.c"
#undef main
#undef CHECK

static void setup(void) {
  log_init(NULL, LOG_ERROR, false, false);
}

TestSuite(otel_adapter, .init = setup);
Test(otel_adapter, bounded_sampling) { bounded_sampling(); }
Test(otel_adapter, dropped_errors) { dropped_errors(); }
Test(otel_adapter, retained_budget) { retained_budget(); }
Test(otel_adapter, sqlite_prepare) { sqlite_prepare_contract(); }
Test(otel_adapter, query_catalog) { catalog_contract(); }
