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

unsigned short int width       = 160,
                   height      = 0,
                   auto_width  = 0,
                   auto_height = 1;

char* opt_ipaddress = "0.0.0.0";

int opt_verbose = 0,
    opt_port    = 0,
    opt_color   = 0;

char ascii_palette[ASCII_PALETTE_SIZE + 1] =
    "   ...',;:clodxkO0KXNWM";

unsigned short int RED[ASCII_PALETTE_SIZE],
                   GREEN[ASCII_PALETTE_SIZE],
                   BLUE[ASCII_PALETTE_SIZE],
                   GRAY[ASCII_PALETTE_SIZE];


void options(int argc, char** argv) {
    precalc_rgb(weight_red, weight_green, weight_blue);

    int opt;
    while (1) {
        static struct option long_options[] = {
            {"verbose", no_argument, &opt_verbose, 1},
            {"port",    no_argument, &opt_port,    0},
            {0, 0, 0, 0}
        };

        /* getopt_long stores the option index here. */
        int option_index = 0;

        opt = getopt_long(
            argc, argv, "", long_options, &option_index);

        /* Detect the end of the options. */
        if (opt == -1)
            break;
    }
}

void options_opthelp() {
}

void precalc_rgb(const float red, const float green, const float blue) {
    for (int n = 0; n < ASCII_PALETTE_SIZE; ++n) {
        RED[n]   = ((float) n) * red;
        GREEN[n] = ((float) n) * green;
        BLUE[n]  = ((float) n) * blue;
        GRAY[n]  = ((float) n);
    }
}

