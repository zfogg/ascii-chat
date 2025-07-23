#include "options.h"
#include "round.h"


// Calculate width or height, but not both
void aspect_ratio(const int jpeg_w, const int jpeg_h) {

    // The 2.0f and 0.5f factors are used for text displays that (usually)
    // have characters that are taller than they are wide.
    // This means we need to scale width by 2.0 and height by 0.5 to maintain visual aspect ratio
    #define CALC_W ROUND(2.0f * (float) opt_height * (float) jpeg_w / (float) jpeg_h)
    #define CALC_H ROUND(0.5f * (float) opt_width  * (float) jpeg_h / (float) jpeg_w)

    // If both dimensions are specified, we don't need to calculate anything
    if (!auto_width && !auto_height) {
        return;
    }

    // If both dimensions are auto, we need to set one first
    if (auto_width && auto_height) {
        // If we have a height set (e.g., from terminal), use it to calculate width
        if (opt_height > 0) {
            opt_width = CALC_W;
            
            // Ensure we have valid dimensions
            while (opt_width == 0) {
                ++opt_height;
                opt_width = CALC_W;
            }
        } else {
            // Start with a reasonable height and calculate width
            opt_height = 50; // Default starting height
            opt_width = CALC_W;
            
            // Ensure we have valid dimensions
            while (opt_width == 0) {
                ++opt_height;
                opt_width = CALC_W;
            }
        }
        return;
    }

    // calc width based on height
    if (auto_width && !auto_height) {
        opt_width = CALC_W;

        // adjust for too small dimensions
        while (opt_width == 0) {
            ++opt_height;
            opt_width = CALC_W;
        }
    }

    // calc height based on width
    if (!auto_width && auto_height) {
        opt_height = CALC_H;
        
        // adjust for too small dimensions
        while (opt_height == 0) {
            ++opt_width;
            opt_height = CALC_H;
        }
    }
}
