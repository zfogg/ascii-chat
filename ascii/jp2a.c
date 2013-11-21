/*
 * Copyright (C) 2006 Christian Stigen Larsen, http://csl.sublevel3.org
 * Distributed under the GNU General Public License (GPL) v2.
 *
 * Project homepage on http://jp2a.sf.net
 *
 * $Id: jp2a.c 465 2006-10-02 11:34:48Z csl $
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../headers/jp2a.h"
#include "../headers/options.h"
#include "../headers/image.h"


//int main(int argc, char** argv) {
//    ascii_init(argc, argv);
//    return 0;
//}

int ascii_init(int argc, char** argv) {
    int store_width, store_height, store_autow, store_autoh;
    FILE *fout = stdout;
    FILE *fp;
    int n;

    parse_options(argc, argv);

    store_width = width;
    store_height = height;
    store_autow = auto_width;
    store_autoh = auto_height;

    if ( strcmp(fileout, "-") ) {
        if ( (fout = fopen(fileout, "wb")) == NULL ) {
            fprintf(stderr, "Could not open '%s' for writing.\n", fileout);
            return 1;
        }
    }

    for ( n=1; n<argc; ++n ) {

        width = store_width;
        height = store_height;
        auto_width = store_autow;
        auto_height = store_autoh;

        // read files
        if ((fp = fopen(argv[n], "rb")) != NULL ) {
            image_t *i = image_read(fp);
            fclose(fp);

            image_t *s = image_new(width, height);
            image_clear(s);
            image_resize(i, s);

            image_print(s);

            image_destroy(i);
            image_destroy(s);

            continue;
        } else {
            fprintf(stderr, "Can't open %s\n", argv[n]);
            return 1;
        }
    }

    if (fout != stdout )
        fclose(fout);

    return 0;
}
