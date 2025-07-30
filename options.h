#include <stdio.h>

#define OPTIONS_BUFF_SIZE 256
#define LUMINANCE_LEVELS 256

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
unsigned short int opt_webcam_flip;

// Color output option
extern unsigned short int opt_color_output;

// Global variables to store last known image dimensions for aspect ratio
// recalculation
extern
unsigned short int last_image_width,
                   last_image_height;


extern
unsigned short int RED  [],
                   GREEN[],
                   BLUE [],
                   GRAY [];


void options_init(int, char **);

void usage(FILE* out_stream);

void precalc_rgb(const float, const float, const float);

// Frame buffer size calculation
size_t get_frame_buffer_size(void);

// Terminal size detection functions
int get_terminal_size(unsigned short int *width, unsigned short int *height);
void update_dimensions_for_full_height(void);
void recalculate_aspect_ratio_on_resize(void);
