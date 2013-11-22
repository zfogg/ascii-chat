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
//    ascii_init(argc, argv,
//        "/home/zfogg/code/c/jp2a2/imgs2/frame_000.jpg");
//    return 0;
//}

char* ascii_test_string(char* filename) {
    FILE *fp;

    if ((fp = fopen(filename, "rb")) != NULL ) {
        image_t *i = image_read(fp);
        fclose(fp);

        image_t *s = image_new(width, height);
        image_clear(s);
        image_resize(i, s);

        image_print(s);

        image_destroy(i);
        image_destroy(s);

        return -1;
    } else {
        printf("err: can't read file\n");
        return -1;
    }
}

int ascii_init(int argc, char** argv) {

    parse_options(argc, argv);

    return 0;
}
