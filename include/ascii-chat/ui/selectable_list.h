#pragma once

#include <stdbool.h>

/** Shared cursor and viewport state for selectable terminal lists. */
/**
 * @brief ui selectable list keeps the values that move through the subsystem: `selected`, `first_visible`. Some members are pointers into caller- or subsystem-owned memory, so lifetime follows the API that populated them rather than the struct copy itself.
 */
typedef struct {
  int selected;
  int first_visible;
} ui_selectable_list_t;

void ui_selectable_list_reset(ui_selectable_list_t *list);
void ui_selectable_list_move(ui_selectable_list_t *list, int count, int rows, int delta);
void ui_selectable_list_keep_visible(ui_selectable_list_t *list, int count, int rows);
