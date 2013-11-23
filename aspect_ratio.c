/*
 * Copyright (C) 2006 Christian Stigen Larsen, http://csl.sublevel3.org
 * Distributed under the GNU General Public License (GPL) v2.
 *
 * Project homepage on http://jp2a.sf.net
 *
 * $Id: aspect_ratio.c 466 2006-10-02 11:35:03Z csl $
 */

#include "options.h"
#include "round.h"

// Calculate width or height, but not both
void aspect_ratio(const int jpeg_w, const int jpeg_h) {

    // The 2.0f and 0.5f factors are used for text displays that (usually)
    // have characters that are taller than they are wide.
    #define CALC_W ROUND(2.0f * (float) height * (float) jpeg_w / (float) jpeg_h)
    #define CALC_H ROUND(0.5f * (float) width  * (float) jpeg_h / (float) jpeg_w)

    // calc width
    if (auto_width && !auto_height) {
        width = CALC_W;

        // adjust for too small dimensions
        while ( width==0 ) {
            ++height;
            aspect_ratio(jpeg_w, jpeg_h);
        }

    }

    // calc height
    if ( !auto_width && auto_height ) {
        height = CALC_H;
        // adjust for too small dimensions
        while ( height==0 ) {
            ++width;
            aspect_ratio(jpeg_w, jpeg_h);
        }
    }
}
