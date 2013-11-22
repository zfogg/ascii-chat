#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include <curses.h>

#include "../headers/ascii.h"

#include "../headers/image.h"
#include "../headers/options.h"

#include "../headers/webcam.hpp"


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
    usleep(30000);
}

void ascii_write_destroy() {
    endwin();
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

