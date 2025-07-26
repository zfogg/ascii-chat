#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "webcam.hpp"
#include "ascii.h"
#include "image.h"
#include "options.h"
#include "common.h"

/* ============================================================================
 * ASCII Art Video Processing
 * ============================================================================ */

asciichat_error_t ascii_read_init(unsigned short int webcam_index) {
    log_info("Initializing ASCII reader with webcam index %u", webcam_index);
    webcam_init(webcam_index);
    return ASCIICHAT_OK;
}

asciichat_error_t ascii_write_init(void) {
    console_clear();
    cursor_reset();
    cursor_hide();
    log_debug("ASCII writer initialized");
    return ASCIICHAT_OK;
}

char *ascii_read(void) {
    FILE *jpeg = webcam_read();

    if (jpeg == NULL) {
        // Return a simple error message if webcam read fails
        log_error("Webcam capture failed");
        printf("%s", "Webcam capture failed\n");
        return NULL;
    }

    image_t *original = image_read(jpeg);
    if (!original) {
        log_error("Failed to read JPEG image");
        fclose(jpeg);
        return NULL;
    }
    
    image_t *resized = image_new(opt_width, opt_height);
    if (!resized) {
        log_error("Failed to allocate resized image");
        image_destroy(original);
        fclose(jpeg);
        return NULL;
    }

    fclose(jpeg);

    image_clear(resized);
    image_resize(original, resized);

    char *ascii = image_print(resized);
    if (!ascii) {
        log_error("Failed to convert image to ASCII");
    }

    image_destroy(original);
    image_destroy(resized);

    return ascii;
}

asciichat_error_t ascii_write(const char *frame) {
    if (frame == NULL) { 
        log_warn("Attempted to write NULL frame");
        return ASCIICHAT_ERR_INVALID_PARAM;
    }

    const char *current = frame;
    const char *segment_start = frame;

    while (*current != 0) {
        if (*current == ASCII_DELIMITER) {
            // Output the segment before this tab
            size_t len = current - segment_start;
            if (len > 0) {
                if (fwrite(segment_start, 1, len, stdout) != len) {
                    log_error("Failed to write ASCII frame segment");
                    return ASCIICHAT_ERR_TERMINAL;
                }
            }

            cursor_reset();
            segment_start = current + 1;  // Next segment starts after tab
        }
        current++;
    }

    // Output the final segment
    size_t remaining = current - segment_start;
    if (remaining > 0) {
        if (fwrite(segment_start, 1, remaining, stdout) != remaining) {
            log_error("Failed to write final ASCII frame segment");
            return ASCIICHAT_ERR_TERMINAL;
        }
    }
    
    return ASCIICHAT_OK;
}

void ascii_write_destroy(void) {
    console_clear();
    cursor_reset();
    cursor_show();
    log_debug("ASCII writer destroyed");
}

void ascii_read_destroy(void) {
    console_clear();
    cursor_reset();
    webcam_cleanup();
    log_debug("ASCII reader destroyed");
}
