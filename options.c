#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ascii.h"
#include "options.h"


// Default options
int verbose           = 0,
    auto_height       = 1,
    auto_width        = 0,

    width             = 78,
    height            = 0,

    use_border        = 0,

    invert            = 1,

    flipx             = 0,
    flipy             = 0,

    colorfill         = 0,
    convert_grayscale = 0,

    debug             = 0,

    clearscr          = 0,

    term_width        = 0,
    term_height       = 0,

    usecolors         = 0,

    termfit           = 0;

#define ASCII_PALETTE_SIZE 256
char ascii_palette[ASCII_PALETTE_SIZE + 1] = "   ...',;:clodxkO0KXNWM";

// Default weights, must add up to 1.0
float redweight = 0.2989f;
float greenweight = 0.5866f;
float blueweight = 0.1145f;

// Calculated in parse_options
unsigned short int RED[256], GREEN[256], BLUE[256], GRAY[256];

const char *fileout = "-"; // stdout

void precalc_rgb(const float red, const float green, const float blue) {
    for (int n = 0; n < 256; ++n) {
        RED[n]   = ((float) n) * red;
        GREEN[n] = ((float) n) * green;
        BLUE[n]  = ((float) n) * blue;
        GRAY[n]  = ((float) n);
    }
}

void parse_options(int argc, char** argv) {
    // make code more readable
    #define IF_OPTS(sopt, lopt)     if ( !strcmp(s, sopt) || !strcmp(s, lopt) )
    #define IF_OPT(sopt)            if ( !strcmp(s, sopt) )
    #define IF_VARS(format, v1, v2) if ( sscanf(s, format, v1, v2) == 2 )
    #define IF_VAR(format, v1)      if ( sscanf(s, format, v1) == 1 )

    for (int n = 1, files = 0; n < argc; ++n) {
        const char *s = argv[n];

        if ( *s != '-' ) { // count files to read
            ++files; continue;
        }

        IF_OPT ("-")                        { ++files; continue; }
        IF_OPTS("-v", "--verbose")          { verbose = 1; continue; }
        IF_OPTS("-d", "--debug")            { debug = 1; continue; }
        IF_OPT ("--clear")                  { clearscr = 1; continue; }
        IF_OPTS("--color", "--colors")      { usecolors = 1; continue; }
        IF_OPT ("--fill")                   { colorfill = 1; continue; }
        IF_OPT ("--grayscale")              { usecolors = 1; convert_grayscale = 1; continue; }
        IF_OPTS("-b", "--border")           { use_border = 1; continue; }
        IF_OPTS("-i", "--invert")           { invert = !invert; continue; }
        IF_OPT("--background=dark")         { invert = 1; continue; }
        IF_OPT("--background=light")        { invert = 0; continue; }
        IF_OPTS("-x", "--flipx")            { flipx = 1; continue; }
        IF_OPTS("-y", "--flipy")            { flipy = 1; continue; }
        IF_VAR ("--width=%d", &width)       { auto_height += 1; continue; }
        IF_VAR ("--height=%d", &height)     { auto_width += 1; continue; }
        IF_VAR ("--red=%f", &redweight)     { continue; }
        IF_VAR ("--green=%f", &greenweight) { continue; }
        IF_VAR ("--blue=%f", &blueweight)   { continue; }

        IF_VARS("--size=%dx%d",&width, &height) {
            auto_width = auto_height = 0; continue;
        }

        if ( !strncmp(s, "--output=", 9) ) {
            fileout = s+9;
            continue;
        }

        if ( !strncmp(s, "--chars=", 8) ) {

            if ( strlen(s-8) > ASCII_PALETTE_SIZE ) {
                fprintf(stderr,
                    "Too many ascii characters specified (max %d)\n",
                    ASCII_PALETTE_SIZE);
                exit(1);
            }

            // don't use sscanf, we need to read spaces as well
            strcpy(ascii_palette, s+8);
            continue;
        }

        fprintf(stderr, "Unknown option %s\n\n", s);
        exit(1);

    } // args ...

    // only --width specified, calc width
    if ( auto_width==1 && auto_height == 1 )
        auto_height = 0;

    // --width and --height is the same as using --size
    if ( auto_width==2 && auto_height==1 )
        auto_width = auto_height = 0;

    precalc_rgb(redweight, greenweight, blueweight);
}
