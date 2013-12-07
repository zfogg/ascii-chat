#include "options.h"
#include "round.h"


// Calculate width or height, but not both
void aspect_ratio(const int jpeg_w, const int jpeg_h) {

    // The 2.0f and 0.5f factors are used for text displays that (usually)
    // have characters that are taller than they are wide.
    #define CALC_W ROUND(2.0f * (float) opt_height * (float) jpeg_w / (float) jpeg_h)
    #define CALC_H ROUND(0.5f * (float) opt_width  * (float) jpeg_h / (float) jpeg_w)

    // calc width
    if (auto_width && !auto_height) {
        opt_width = CALC_W;

        // adjust for too small dimensions
        while (opt_width == 0) {
            ++opt_height;
            aspect_ratio(jpeg_w, jpeg_h);
        }
    }

    // calc height
    if ( !auto_width && auto_height ) {
        opt_height = CALC_H;
        // adjust for too small dimensions
        while (opt_height == 0) {
            ++opt_width;
            aspect_ratio(jpeg_w, jpeg_h);
        }
    }
}
