#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "webcam.hpp"

#include "ascii.h"
#include "image.h"
#include "options.h"


void ascii_read_init(unsigned short int webcam_index) {
    webcam_init(webcam_index);
}

void ascii_write_init() {
    console_clear();
    cursor_reset();
    cursor_hide();
}

char *ascii_read() {
    FILE *jpeg = webcam_read();
    
    if (jpeg == NULL) {
        // Return a simple error message if webcam read fails
        printf("%s", "Webcam capture failed\n");
        return NULL;
    }

    image_t *original = image_read(jpeg);
    image_t *resized  = image_new(opt_width, opt_height);

    fclose(jpeg);

    image_clear(resized);
    image_resize(original, resized);

    char *ascii = image_print(resized);

    image_destroy(original);
    image_destroy(resized);

    return ascii;
}

void ascii_write(char *f) {
    if (f == nullptr) { return; }  // Safety check
    
    char *current = f;
    char *segment_start = f;
    
    while (*current != 0) {
        if (*current == ASCII_DELIMITER) {
            // Output the segment before this tab
            size_t len = current - segment_start;
            if (len > 0) {
                fwrite(segment_start, 1, len, stdout);
            }
            
            cursor_reset();
            segment_start = current + 1;  // Next segment starts after tab
        }
        current++;
    }
    
    // Output the final segment
    size_t remaining = current - segment_start;
    if (remaining > 0) {
        fwrite(segment_start, 1, remaining, stdout);
    }
}

void ascii_write_destroy() {
    console_clear();
    cursor_reset();
    cursor_show();
}

void ascii_read_destroy() {
    console_clear();
    cursor_reset();
    webcam_cleanup();
}
