#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include <curses.h>

#include "webcam.hpp"

#include "ascii.h"
#include "image.h"
#include "options.h"


#define _POSIX_C_SOURCE 200809L


struct timespec tim = {
    .tv_sec  = 0,
    .tv_nsec = 0
}, tim2 = {
    .tv_sec  = 0,
    .tv_nsec = 0
};


static inline
void zzz() {
    tim.tv_sec  = 0;
    tim.tv_nsec = 500;
    nanosleep(&tim, &tim2);
}

void ascii_read_init() {
    parse_options(0, NULL); // computer globals 'width' and 'height'
    webcam_init();
}

void ascii_write_init() {
    // Init curses
    initscr();
    noecho();
    cbreak();
    nodelay(stdscr, true);
    keypad(stdscr, true);
    curs_set(0); // Invisible cursor.
}

char *ascii_read() {
    return ascii_from_jpeg(webcam_read());
}

void ascii_write(char *f) {
    for (int i = 0; f[i] != '\0'; ++i)
        if (f[i] == '\t') {
            move(0, 0);
        } else
            addch(f[i]);
    refresh();
    zzz();
}

void ascii_write_destroy() {
    endwin();
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

