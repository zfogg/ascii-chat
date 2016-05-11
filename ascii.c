#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "webcam.hpp"

#include "ascii.h"
#include "image.h"
#include "options.h"


void ascii_read_init() {
    webcam_init();
}

void ascii_write_init() {
    console_clear();
    cursor_reset();
    cursor_hide();
}

char *ascii_read() {
    FILE *jpeg = webcam_read();

    image_t *original = image_read(jpeg),
            *resized  = image_new(opt_width, opt_height);

    fclose(jpeg);

    image_clear(resized);
    image_resize(original, resized);

    char *ascii = image_print(resized);

    image_destroy(original);
    image_destroy(resized);

    return ascii;
}

void ascii_write(char *f) {
    for (int i = 0; f[i] != '\0'; ++i)
        if (f[i] == '\t')
            cursor_reset();
        else
            putchar(f[i]);
    ascii_zzz();
}

void ascii_write_destroy() {
    console_clear();
    cursor_reset();
    cursor_show();
}

void ascii_read_destroy() {
}

