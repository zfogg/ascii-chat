#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "webcam.hpp"

#include "ascii.h"
#include "image.h"
#include "options.h"


static inline
void zzz() {
    nanosleep(&tim, &tim2);
}

void ascii_read_init() {
    parse_options(0, NULL);
    // ^ compute globals 'width' and 'height'
    webcam_init();
}

void ascii_write_init() {
    console_clear();
    cursor_reset();
    cursor_hide();
}

char *ascii_read() {
    return ascii_from_jpeg(webcam_read());
}

void ascii_write(char *f) {
    for (int i = 0; f[i] != '\0'; ++i)
        if (f[i] == '\t')
            cursor_reset();
        else
            putchar(f[i]);
    zzz();
}

void ascii_write_destroy() {
    console_clear();
    cursor_reset();
    cursor_show();
}

void ascii_read_destroy() {
}

char *ascii_from_jpeg(FILE *fp) {
    char *out = NULL;

    image_t *img = image_read(fp);
    fclose(fp);

    image_t *s = image_new(width, height);
    image_clear(s);
    image_resize(img, s);
    out = image_print(s);
    image_destroy(img);
    image_destroy(s);

    return out;
}

