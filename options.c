#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "ascii.h"
#include "options.h"


// Default weights; must add up to 1.0
static const float weight_red   = 0.2989f;
static const float weight_green = 0.5866f;
static const float weight_blue  = 0.1145f;

unsigned short int opt_width   = 80,
                   opt_height  = 0,

                   auto_width  = 0,
                   auto_height = 1;

char opt_address[OPTIONS_BUFF_SIZE] = "0.0.0.0",
     opt_port   [OPTIONS_BUFF_SIZE] = "9001";

unsigned short int opt_webcam_index = 0;

char ascii_palette[ASCII_PALETTE_SIZE + 1] =
    "   ...',;:clodxkO0KXNWM";

unsigned short int RED  [ASCII_PALETTE_SIZE],
                   GREEN[ASCII_PALETTE_SIZE],
                   BLUE [ASCII_PALETTE_SIZE],
                   GRAY [ASCII_PALETTE_SIZE];

static struct option long_options[] = {
    {"address",       required_argument, NULL, 'a'},
    {"port",          required_argument, NULL, 'p'},
    {"width",         optional_argument, NULL, 'x'},
    {"height",        optional_argument, NULL, 'y'},
    {"webcam-index",  required_argument, NULL, 'c'},
    {"help",          optional_argument, NULL, 'h'},
    {0, 0, 0, 0}
};


void options_init(int argc, char** argv) {
    precalc_rgb(weight_red, weight_green, weight_blue);
    while (1) {
        int index  = 0,
            c = getopt_long(argc, argv, "a:p:x:y:c:h", long_options, &index);
        if (c == -1)
            break;

        char argbuf[1024];
        switch (c) {
            case 0:
                break;

            case 'a':
                snprintf(opt_address, OPTIONS_BUFF_SIZE, "%s", optarg);
                break;

            case 'p':
                snprintf(opt_port, OPTIONS_BUFF_SIZE, "%s", optarg);
                break;

            case 'x':
                snprintf(argbuf, OPTIONS_BUFF_SIZE, "%s", optarg);
                opt_width = strtoint(argbuf);
                break;

            case 'y':
                snprintf(argbuf, OPTIONS_BUFF_SIZE, "%s", optarg);
                opt_height = strtoint(argbuf);
                break;

            case 'c':
                snprintf(argbuf, OPTIONS_BUFF_SIZE, "%s", optarg);
                opt_webcam_index = strtoint(argbuf);
                break;

            case '?':
                fprintf(stderr, "Unknown option %c\n", optopt);
                usage(stderr);
                exit(EXIT_FAILURE);
                break;

            case 'h':
                usage(stdout);
                exit(EXIT_SUCCESS);
                break;

            default:
                abort();
        }
    }
}

void usage(FILE* desc /* stdout|stderr*/ ) {
    fprintf(desc, "ascii-chat\n");
    fprintf(desc, "\toptions:\n");
    fprintf(desc, "\t\t -a --address      (server|client) \t ip address\n");
    fprintf(desc, "\t\t -p --port         (server|client) \t tcp port\n");
    fprintf(desc, "\t\t -x --width        (client) \t     render width\n");
    fprintf(desc, "\t\t -y --height       (client) \t     render height\n");
    fprintf(desc, "\t\t -c --webcam-index (server) \t     webcam device index\n");
    fprintf(desc, "\t\t -h --help         (server|client) \t print this help\n");
}

void precalc_rgb(const float red, const float green, const float blue) {
    for (int n = 0; n < ASCII_PALETTE_SIZE; ++n) {
        RED[n]   = ((float) n) * red;
        GREEN[n] = ((float) n) * green;
        BLUE[n]  = ((float) n) * blue;
        GRAY[n]  = ((float) n);
    }
}
