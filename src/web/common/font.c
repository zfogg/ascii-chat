#include "font.h"
#include <ascii-chat/font.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>

/* Browser terminal rendering reads the embedded font directly from WASM memory. */
const unsigned char *get_font_default_ptr(int selection) {
  bool matrix = selection == 0 ? GET_OPTION(matrix_rain) : selection == 2;
  return matrix ? g_font_matrix_resurrected : g_font_default;
}

unsigned int get_font_default_size(int selection) {
  bool matrix = selection == 0 ? GET_OPTION(matrix_rain) : selection == 2;
  return (unsigned int)(matrix ? g_font_matrix_resurrected_size : g_font_default_size);
}
