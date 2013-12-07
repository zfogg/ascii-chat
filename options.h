#define ASCII_PALETTE_SIZE 256

#define OPTIONS_BUFF_SIZE 256


extern
char ascii_palette[];

extern
unsigned short int width,
                   height,
                   auto_width,
                   auto_height;

extern
char opt_address[],
     opt_port   [];

extern
char ascii_palette[];

extern
unsigned short int RED  [],
                   GREEN[],
                   BLUE [],
                   GRAY [];


void options_init(int, char **);

void precalc_rgb(const float, const float, const float);
