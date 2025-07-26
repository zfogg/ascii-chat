#include <stdio.h>
#include <time.h>
#include "common.h"


asciichat_error_t ascii_read_init(unsigned short int webcam_index);
asciichat_error_t ascii_write_init(void);

char *ascii_read(void);
asciichat_error_t ascii_write(const char *);

void ascii_read_destroy(void);
void ascii_write_destroy(void);

/*static char *from_jpeg(FILE *);*/


#define ASCII_DELIMITER '\t'

#define ASCII_SLEEP_NS 50000L


#define print(s) fwrite(s, 1, sizeof(s)/sizeof(s[0]), stdout)

#define console_clear() print("\e[1;1H\e[2J")

#define cursor_reset()  print("\033[0;0H")

#define cursor_hide()   print("\e[?25l")

#define cursor_show()   print("\e[?25h")


static const struct timespec
ASCII_SLEEP_START = {
    .tv_sec  = 0,
    .tv_nsec = 500
},
ASCII_SLEEP_STOP = {
    .tv_sec  = 0,
    .tv_nsec = 0
};

#define ascii_zzz() nanosleep( \
    (struct timespec *)&ASCII_SLEEP_START, \
    (struct timespec *)&ASCII_SLEEP_STOP)
