#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/tui/too_small.h>

#include <limits.h>
#include <stdio.h>

#define CHECK(expression)                                                                                              \
  do {                                                                                                                 \
    if (!(expression)) {                                                                                                \
      fprintf(stderr, "render surface grid check failed: %s\n", #expression);                                      \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

int main(void) {
  int cols = 0;
  int rows = 0;
  CHECK(term_renderer_calculate_surface_grid(1200, 2400, 22.0, 80, 40, &cols, &rows) == ASCIICHAT_OK);
  CHECK(cols == 80 && rows == 40);

  CHECK(term_renderer_calculate_surface_grid(1200, 2400, 28.0, 90, 0, &cols, &rows) == ASCIICHAT_OK);
  CHECK(cols == 90 && rows > 0);

  CHECK(term_renderer_calculate_surface_grid(1200, 2400, 28.0, 0, 42, &cols, &rows) == ASCIICHAT_OK);
  CHECK(cols > 0 && rows == 42);

  /* A large native-display font must still produce dimensions accepted by
   * the shared client-like option parser. */
  CHECK(term_renderer_calculate_surface_grid(1206, 2622, 112.0, 0, 0, &cols, &rows) == ASCIICHAT_OK);
  CHECK(cols == UI_MEDIA_MIN_COLS && rows >= UI_MEDIA_MIN_ROWS);

  CHECK(term_renderer_calculate_surface_grid(INT_MAX, INT_MAX, 0.000001, 0, 0, &cols, &rows) != ASCIICHAT_OK);
  CHECK(term_renderer_calculate_surface_grid(0, 2400, 22.0, 0, 0, &cols, &rows) != ASCIICHAT_OK);
  return 0;
}
