#include <stdlib.h>
#include <stdbool.h>
#include <curses.h>


void setupCurses() {
    initscr();
    noecho();
    cbreak();
    nodelay(stdscr, true);
    keypad(stdscr, true);
}

void drawLine(char* s, bool toStart) {
    if (toStart) { // ch == '\t'
        move(0, 0);
    } else {
        printw(s);
        refresh();
    }
}

void killCurses() {
    endwin();
}

