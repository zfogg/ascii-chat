#include <criterion/criterion.h>
#include <ascii-chat/ui/fps_counter.h>

Test(fps_counter, startup_and_rates) {
  fps_counter_t *counter = fps_counter_create();
  cr_assert_not_null(counter);
  cr_assert_eq(fps_counter_get_at(counter, 0), 0);
  const unsigned rates[] = {1, 9, 10, 60, 99, 100, 144};
  for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
    fps_counter_reset(counter);
    uint64_t step = 1000000000ULL / rates[r];
    for (unsigned i = 0; i < 70; i++)
      fps_counter_tick_at(counter, i * step);
    cr_assert_float_eq(fps_counter_get_at(counter, 69 * step), rates[r], 0.01);
  }
  fps_counter_destroy(counter);
}

Test(fps_counter, inactivity_and_clock_reset) {
  fps_counter_t *counter = fps_counter_create();
  fps_counter_tick_at(counter, 0);
  fps_counter_tick_at(counter, 10000000);
  cr_assert_float_eq(fps_counter_get_at(counter, 10000000), 100, 0.01);
  cr_assert_eq(fps_counter_get_at(counter, 2010000000), 0);
  fps_counter_tick_at(counter, 3000000000);
  cr_assert_eq(fps_counter_get_at(counter, 3000000000), 0);
  fps_counter_tick_at(counter, 3010000000);
  cr_assert_float_eq(fps_counter_get_at(counter, 3010000000), 100, 0.01);
  fps_counter_tick_at(counter, 1);
  cr_assert_eq(fps_counter_get_at(counter, 1), 0);
  fps_counter_tick_at(counter, 1);
  cr_assert_eq(fps_counter_get_at(counter, 1), 0);
  fps_counter_reset(counter);
  cr_assert_eq(fps_counter_get_at(counter, 1), 0);
  fps_counter_destroy(counter);
}
