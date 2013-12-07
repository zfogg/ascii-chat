#define ASCII_PALETTE_SIZE 256


extern
char ascii_palette[];

extern
unsigned short int width,
                   height,
                   auto_width,
                   auto_height;

extern
char* opt_ipaddress;

extern
int opt_verbose,
    opt_port,
    opt_color;

extern
char ascii_palette[];

extern
unsigned short int RED[],
                   GREEN[],
                   BLUE[],
                   GRAY[];


void options(int, char **);

void precalc_rgb(const float, const float, const float);

void options_opthelp();
