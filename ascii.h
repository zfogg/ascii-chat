#include <time.h>


void ascii_read_init();
void ascii_write_init();

char *ascii_read();
void ascii_write(char *);

void ascii_read_destroy();
void ascii_write_destroy();

char *ascii_from_jpeg(FILE *);


#define ASCII_DELIMITER '\t'

#define ASCII_SLEEP_NS 50000L


#define print(s) fwrite(s, 1, sizeof(s)/sizeof(s[0]), stdout)

#define console_clear() print("\e[1;1H\e[2J")

#define cursor_reset()  print("\033[0;0H")

#define cursor_hide()   print("\e[?25l")

#define cursor_show()   print("\e[?25h")


static const struct timespec tim = {
    .tv_sec  = 0,
    .tv_nsec = 500
}, tim2 = {
    .tv_sec  = 0,
    .tv_nsec = 0
};
