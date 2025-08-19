#pragma once

#include <stdio.h>

#define OPTIONS_BUFF_SIZE 256

#define strtoint(s) (int)strtol(s, (char **)NULL, 10)

extern unsigned short int opt_width, opt_height, auto_width, auto_height;

extern char opt_address[], opt_port[];

extern unsigned short int opt_webcam_index;

extern unsigned short int opt_webcam_flip;

extern unsigned short int opt_color_output;

extern unsigned short int opt_background_color;

extern unsigned short int opt_audio_enabled;

// If non-zero, allow image to stretch or shrink without preserving aspect ratio
extern unsigned short int opt_stretch;

// If non-zero, disable console logging (quiet mode)
extern unsigned short int opt_quiet;

// If non-zero, enable snapshot mode (client only - capture one frame and exit)
extern unsigned short int opt_snapshot_mode;

// Snapshot delay in seconds (float) - default 3.0 for webcam warmup
extern float opt_snapshot_delay;

// Log file path for file logging (empty string means no file logging)
extern char opt_log_file[OPTIONS_BUFF_SIZE];

// Encryption options
extern unsigned short int opt_encrypt_enabled;      // Enable AES encryption
extern char opt_encrypt_key[OPTIONS_BUFF_SIZE];     // Encryption key from --key
extern char opt_encrypt_keyfile[OPTIONS_BUFF_SIZE]; // Key file path from --keyfile
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
