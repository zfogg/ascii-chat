#include <stdio.h>

#define ASCII_PALETTE_SIZE 256

#define OPTIONS_BUFF_SIZE 256

#define strtoint(s) (int)strtol(s, (char **)NULL, 10)


extern
char ascii_palette[];

extern
unsigned short int opt_width,
                   opt_height,
                   auto_width,
                   auto_height;

extern
char opt_address[],
     opt_port   [];

extern
unsigned short int opt_webcam_index;

extern
char ascii_palette[];

extern
unsigned short int RED  [],
                   GREEN[],
                   BLUE [],
                   GRAY [];


void options_init(int, char **);

void usage(FILE* out_stream);

void precalc_rgb(const float, const float, const float);

// Terminal size detection functions
int get_terminal_size(unsigned short int *width, unsigned short int *height);
void update_dimensions_for_full_height(void);
