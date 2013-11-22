#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include <curses.h>

#include "../headers/ascii.h"
#include "../headers/webcam.hpp"

#include "../headers/image.h"
#include "../headers/options.h"


void ascii_init_read(int argc, char **argv) {
    // Compute the 'width' and 'height' variables:
    parse_options(argc, argv);
    ascii_init_cam();
}

void ascii_init_write() {
    // Init curses
    initscr();
    noecho();
    cbreak();
    nodelay(stdscr, true);
    keypad(stdscr, true);
    curs_set(0); // Invisible cursor.
}

void ascii_destroy_write() {
    endwin();
}

char *ascii_read(char *filename) {
    return ascii_cam_read();
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
