#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <jpeglib.h>

#include "ascii.h"
#include "aspect_ratio.h"
#include "image.h"
#include "options.h"
#include "round.h"


typedef void (*image_resize_ptrfun)(const image_t* , image_t*);
image_resize_ptrfun global_image_resize_fun = NULL;


image_t* image_new(int width, int height) {
    image_t *p;

    if (!(p = (image_t *) malloc(sizeof(image_t)))) {
        perror("jp2a: coudln't allocate memory for image");
        exit(EXIT_FAILURE);
    }

    if (!(p->pixels = (rgb_t *) malloc(width*height*sizeof(rgb_t)))) {
        perror("jp2a: couldn't allocate memory for image");
        exit(EXIT_FAILURE);
    }

    p->w = width;
    p->h = height;
    return p;
}

void image_destroy(image_t *p) {
    free(p->pixels);
    free(p);
}

void image_clear(image_t *p) {
    memset(p->pixels, 0, p->w*p->h*sizeof(rgb_t));
}

inline
rgb_t* image_pixel(image_t *p, const int x, const int y) {
    return &p->pixels[x + y*p->w];
}

void image_resize(const image_t *s, image_t *d) {
    global_image_resize_fun(s, d);
}

void image_resize_interpolation(const image_t *source, image_t *dest) {
    register unsigned int r, g, b;

    const int ynrat = (float)source->h / (float)dest->h;
    const int xnrat = (float)source->w / (float)dest->w;

    const int yinc = ynrat*source->w;
    const unsigned int adds = xnrat * ynrat;

    register rgb_t* pix = dest->pixels;
    register rgb_t* pix_next = pix + dest->w;

    register const rgb_t* samp_end;
    register const rgb_t* src = source->pixels;
    register const rgb_t* src_end = source->pixels + dest->h*yinc;

    while ( src < src_end ) {

        const rgb_t *sample_start_plus_yinc = src + yinc;

        while ( pix < pix_next ) {

            r = g = b = 0;
            samp_end = src + xnrat;

            while ( src < sample_start_plus_yinc ) {

                while ( src < samp_end ) {
                    r += src->r;
                    g += src->g;
                    b += src->b;
                    ++src;
                }

                src += source->w - xnrat;
                samp_end += source->w;
            }

            pix->r = r/adds;
            pix->g = g/adds;
            pix->b = b/adds;

            ++pix;
            src -= yinc - xnrat;
        }

        src = sample_start_plus_yinc;
        pix_next += dest->w;
    }
}

image_t *image_read(FILE *fp) {
    JSAMPARRAY buffer;
    int row_stride;
    struct jpeg_decompress_struct jpg;
    struct jpeg_error_mgr jerr;
    image_t *p;

    global_image_resize_fun = image_resize_interpolation;

    jpg.err = jpeg_std_error(&jerr);

    jpeg_create_decompress(&jpg);
    jpeg_stdio_src(&jpg, fp);
    jpeg_read_header(&jpg, TRUE);
    jpeg_start_decompress(&jpg);

    if (jpg.data_precision != 8) {
        fprintf(stderr, "jp2a: can only handle 8-bit color channels\n");
        exit(1);
    }

    row_stride = jpg.output_width * jpg.output_components;
    buffer = (*jpg.mem->alloc_sarray)((j_common_ptr) &jpg, JPOOL_IMAGE, row_stride, 1);

    aspect_ratio(jpg.output_width, jpg.output_height);
    p = image_new(jpg.output_width, jpg.output_height);

    while (jpg.output_scanline < jpg.output_height) {
        jpeg_read_scanlines(&jpg, buffer, 1);

        if (jpg.output_components == 3) {
            memcpy(&p->pixels[(jpg.output_scanline-1)*p->w], &buffer[0][0], sizeof(rgb_t)*p->w);
        } else {
            rgb_t *pixels = &p->pixels[(jpg.output_scanline-1) * p->w];

            // grayscale
            for (int x = 0; x < (int)jpg.output_width; ++x)
                pixels[x].r = pixels[x].g = pixels[x].b = buffer[0][x];
        }
    }

    jpeg_finish_decompress(&jpg);
    jpeg_destroy_decompress(&jpg);
    return p;
}

char* get_lum_palette() {
    static char palette[256];
    for (int n = 0; n < 256; n++) {
        palette[n] = ascii_palette[ROUND(
            (float)(strlen(ascii_palette) - 1) * (float)n / (float)MAXJSAMPLE
        )];
    }
    return palette;
}

char *image_print(const image_t *p) {
    int h = p->h,
        w = p->w;

    rgb_t* pix = p->pixels;
    char* palette = get_lum_palette();

    int len = h*w;
    char* lines = (char*)malloc((len + 2)*sizeof(char));

    lines[len  ] = ASCII_DELIMITER;
    lines[len+1] = '\0';

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++, pix++)
            lines[(y*w)+x] = palette[
                RED[pix->r] + GREEN[pix->g] + BLUE[pix->b]
            ];
        lines[(y*w)+w-1] = '\n';
    }

    return lines;
}
