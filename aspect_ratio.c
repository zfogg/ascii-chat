#include "options.h"
#include "round.h"
#include <stdlib.h>

/*============================================================================
 * Aspect-ratio handling
 *============================================================================
 *
 * Character cells in most monospace fonts are roughly twice as tall as they
 * are wide.  This means that if we render an image 100 characters wide and 100
 * characters tall it will look vertically stretched on-screen.  To compensate
 * we introduce a constant (CHAR_ASPECT) that represents the ratio of the cell
 * height to its width.  With a value of 2.0 the resulting ASCII art will keep
 * the original picture proportions.
 *
 * The user can override this behaviour with the command-line switch
 *   -s / --stretch
 * which sets the global flag `opt_stretch`.  When that flag is non-zero we
 * skip all aspect-ratio corrections so the frame is stretched/shrunk to fill
 * exactly the requested width/height.
 *
 * auto_width / auto_height come from the options parser.  They are non-zero
 * when the corresponding dimension was **not** specified by the user and must
 * therefore be calculated here.
 */

#define CHAR_ASPECT 2.0f  // terminal cell height ÷ width

/* Set opt_width / opt_height so that the ASCII art keeps the webcam image's
 * proportions on screen.
 *
 * jpeg_w, jpeg_h  —— pixel dimensions of the captured frame
 */
void aspect_ratio(const int jpeg_w, const int jpeg_h)
{
    // If the user asked to stretch, do nothing – the caller will use whatever
    // dimensions are currently in opt_width/opt_height.
    if (opt_stretch) {
        return;
    }

    // If both dimensions were given explicitly there's nothing to adjust.
    if (!auto_width && !auto_height) {
        return;
    }

    /* Helper macros – they turn a given height (rows) into a width (columns)
     * and vice-versa, taking CHAR_ASPECT into account.  We use ROUND() from
     * round.h so we end up with integer character counts.
     */
#define CALC_WIDTH_FROM_HEIGHT(h)  ROUND((float)(h) * (float)jpeg_w / (float)jpeg_h * CHAR_ASPECT)
#define CALC_HEIGHT_FROM_WIDTH(w)  ROUND(((float)(w) / CHAR_ASPECT) * (float)jpeg_h / (float)jpeg_w)

    if (auto_width && !auto_height) {
        // Height fixed, width is derived.
        opt_width  = CALC_WIDTH_FROM_HEIGHT(opt_height);
        if (opt_width == 0) {
            opt_width = 1;   // safeguard against zero
        }
        return;
    }

    if (!auto_width && auto_height) {
        // Width fixed, height is derived.
        opt_height  = CALC_HEIGHT_FROM_WIDTH(opt_width);
        if (opt_height == 0) {
            opt_height = 1;
        }
        return;
    }

    /* If both dimensions are automatic we attempt to base the calculation on
     * whichever dimension has already been set by terminal-size detection.
     * Failing that we fall back to a reasonable hard-coded default.
     */
    if (auto_width && auto_height) {
        /* Treat the currently stored opt_width/opt_height as a MAX rectangle
         * we are allowed to use (these came from the client).  Produce the
         * largest possible aspect-correct frame that fits entirely inside it.
         */
        const int max_w = opt_width;
        const int max_h = opt_height;

        int w_from_h = CALC_WIDTH_FROM_HEIGHT(max_h);
        if (w_from_h <= max_w) {
            /* Using the full height still keeps us within the width budget. */
            opt_width  = w_from_h;
            opt_height = max_h;
        } else {
            /* Otherwise scale based on the width limit. */
            opt_width  = max_w;
            opt_height = CALC_HEIGHT_FROM_WIDTH(max_w);
        }

        return;
    }

#undef CALC_WIDTH_FROM_HEIGHT
#undef CALC_HEIGHT_FROM_WIDTH
}
