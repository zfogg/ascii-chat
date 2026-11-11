/** Bundled font selection shared by the browser renderers. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

const unsigned char *get_font_default_ptr(int selection);
unsigned int get_font_default_size(int selection);

#ifdef __cplusplus
}
#endif
