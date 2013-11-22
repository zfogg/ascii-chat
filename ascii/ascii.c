#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../headers/ascii.h"
#include "../headers/image.h"
#include "../headers/options.h"


void ascii_init(int argc, char** argv) {
    parse_options(argc, argv);
}

char *ascii_getline() {
    return "";
}

void ascii_drawline(char *p) {
    (void)p;
}


//int main(int argc, char** argv) {
//    // FIXME: debug
//    ascii_init(argc, argv,
//        "/home/zfogg/code/c/jp2a2/imgs2/frame_000.jpg");
//    return 0;
//}

char* ascii_test_string(char* filename) {
    // FIXME: debug
    FILE *in  = NULL;
    char *out = NULL;

    if ((in = fopen(filename, "rb")) != NULL) {
        image_t *i = image_read(in);
        fclose(in);

        image_t *s = image_new(width, height);
        image_clear(s);
        image_resize(i, s);

        out = image_print(s);

        image_destroy(i);
        image_destroy(s);
    } else {
        printf("err: can't read file\n");
    }

    return out;
}

