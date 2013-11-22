#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include <curses.h>

#include "../headers/ascii.h"

#include "../headers/image.h"
#include "../headers/options.h"


void ascii_init_read(int argc, char **argv) {
    // Compute the 'width' and 'height' variables:
    parse_options(argc, argv);
}

void ascii_init_write() {
    // Init curses
    initscr();
    noecho();
    cbreak();
    nodelay(stdscr, true);
    keypad(stdscr, true);
}

void ascii_destroy_write() {
    endwin();
}

char *ascii_getframe(char *filename) {
    FILE *in  = NULL;
    char *out = NULL;

    if ((in = fopen(filename, "rb")) != NULL) {
        image_t *i = image_read(in);
        fclose(in);

        image_t *s = image_new(width, height);
        image_clear(s);
        image_resize(i, s);

        out = image_print(s);

        image_destroy(i);
        image_destroy(s);
    } else
        printf("err: can't read file\n");

    return out;
}

void ascii_drawframe(char *f) {
    move(0, 0);
    refresh();
    usleep(200000);
    printw(f);
    refresh();
}
