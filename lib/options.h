#include <stdio.h>

#define OPTIONS_BUFF_SIZE 256

#define strtoint(s) (int)strtol(s, (char **)NULL, 10)

extern char ascii_palette[];

extern unsigned short int opt_width, opt_height, auto_width, auto_height;

extern char opt_address[], opt_port[];

extern unsigned short int opt_webcam_index;

extern unsigned short int opt_webcam_flip;

extern unsigned short int opt_color_output;

extern unsigned short int opt_background_color;

extern unsigned short int opt_audio_enabled;

// If non-zero, allow image to stretch or shrink without preserving aspect ratio
extern unsigned short int opt_stretch;

// Default weights; must add up to 1.0
extern const float weight_red;
extern const float weight_green;
extern const float weight_blue;

extern unsigned short int RED[], GREEN[], BLUE[], GRAY[];

void options_init(int, char **);

void usage(FILE *out_stream);

// Terminal size detection functions
int get_terminal_size(unsigned short int *width, unsigned short int *height);
void update_dimensions_for_full_height(void);
void update_dimensions_to_terminal_size(void);
