#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <jpeglib.h>
#include <stdint.h>
#include <stdlib.h>

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

// Optimized interpolation function with better integer arithmetic and memory access
void image_resize_interpolation(const image_t *source, image_t *dest) {
    const int src_w = source->w;
    const int src_h = source->h;
    const int dst_w = dest->w;
    const int dst_h = dest->h;
    
    // Use fixed-point arithmetic for better performance
    const uint32_t x_ratio = ((src_w << 16) / dst_w) + 1;
    const uint32_t y_ratio = ((src_h << 16) / dst_h) + 1;
    
    const rgb_t *src_pixels = source->pixels;
    rgb_t *dst_pixels = dest->pixels;
    
    for (int y = 0; y < dst_h; y++) {
        const uint32_t src_y = (y * y_ratio) >> 16;
        const rgb_t *src_row = src_pixels + src_y * src_w;
        rgb_t *dst_row = dst_pixels + y * dst_w;
        
        for (int x = 0; x < dst_w; x++) {
            const uint32_t src_x = (x * x_ratio) >> 16;
            dst_row[x] = src_row[src_x];
        }
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

    // Store image dimensions for aspect ratio recalculation on terminal resize
    last_image_width = jpg.output_width;
    last_image_height = jpg.output_height;
    
    p = image_new(jpg.output_width, jpg.output_height);

    while (jpg.output_scanline < jpg.output_height) {
        jpeg_read_scanlines(&jpg, buffer, 1);

        if (jpg.output_components == 3) {
            memcpy(&p->pixels[(jpg.output_scanline-1)*p->w], &buffer[0][0], sizeof(rgb_t)*p->w);
        } else {
            rgb_t *pixels = &p->pixels[(jpg.output_scanline-1) * p->w];

            // grayscale - optimized loop
            const JSAMPLE *src = buffer[0];
            for (int x = 0; x < (int)jpg.output_width; ++x) {
                const JSAMPLE gray = src[x];
                pixels[x].r = pixels[x].g = pixels[x].b = gray;
            }
        }
    }

    jpeg_finish_decompress(&jpg);
    jpeg_destroy_decompress(&jpg);
    return p;
}

// Optimized palette generation with caching
static char* cached_palette = NULL;

char* get_lum_palette() {
    if (cached_palette != NULL) {
        return cached_palette;
    }
    
    cached_palette = (char*)malloc(256 * sizeof(char));
    const int palette_len = strlen(ascii_palette) - 1;
    
    for (int n = 0; n < 256; n++) {
        cached_palette[n] = ascii_palette[ROUND(
            (float)palette_len * (float)n / (float)MAXJSAMPLE
        )];
    }
    return cached_palette;
}

// Optimized image printing with better memory access patterns
char *image_print(const image_t *p) {
    const int h = p->h;
    const int w = p->w;
    const int len = h * w;

    const rgb_t *pix = p->pixels;
    const char *palette = get_lum_palette();
    const unsigned short int *red_lut = RED;
    const unsigned short int *green_lut = GREEN;
    const unsigned short int *blue_lut = BLUE;

    char* lines = (char*)malloc((len + 2) * sizeof(char));
    
    lines[len] = ASCII_DELIMITER;
    lines[len + 1] = '\0';

    for (int y = 0; y < h; y++) {
        const int row_offset = y * w;
        
        for (int x = 0; x < w; x++) {
            const rgb_t pixel = pix[row_offset + x];
            const int luminance = red_lut[pixel.r] + green_lut[pixel.g] + blue_lut[pixel.b];
            lines[row_offset + x] = palette[luminance];
        }
        lines[row_offset + w - 1] = '\n';
    }

    return lines;
}

// Color quantization to reduce frame size and improve performance
void quantize_color(int* r, int* g, int* b, int levels) {
    int step = 256 / levels;
    *r = (*r / step) * step;
    *g = (*g / step) * step; 
    *b = (*b / step) * step;
}

// Colored ASCII art printing function with quantization
char *image_print_colored(const image_t *p) {
    const int h = p->h;
    const int w = p->w;
    
    // Calculate buffer size: each character can have color codes (~20 chars per pixel)
    const int estimated_size = h * w * 25 + h * 10 + 100;  // Extra space for newlines and delimiter
    
    char* lines = (char*)malloc(estimated_size * sizeof(char));
    if (!lines) {
        fprintf(stderr, "Failed to allocate memory for colored ASCII output\n");
        return NULL;
    }
    
    const rgb_t *pix = p->pixels;
    const char *palette = get_lum_palette();
    const unsigned short int *red_lut = RED;
    const unsigned short int *green_lut = GREEN;
    const unsigned short int *blue_lut = BLUE;
    
    char *current_pos = lines;
    
    for (int y = 0; y < h; y++) {
        const int row_offset = y * w;
        
        for (int x = 0; x < w; x++) {
            const rgb_t pixel = pix[row_offset + x];
            const int luminance = red_lut[pixel.r] + green_lut[pixel.g] + blue_lut[pixel.b];
            const char ascii_char = palette[luminance];
            
            // Quantize colors to reduce frame size (8 levels = 512 total colors instead of 16M)
            int r = pixel.r, g = pixel.g, b = pixel.b;
            quantize_color(&r, &g, &b, 8);
            
            // Add ANSI color code for foreground
            int written = snprintf(current_pos, 30, "\033[38;2;%d;%d;%dm%c", 
                                 r, g, b, ascii_char);
            current_pos += written;
        }
        
        // Add newline and reset color at end of each row
        int written = snprintf(current_pos, 10, "\033[0m\n");
        current_pos += written;
    }
    
    // Add delimiter and null terminator
    *current_pos++ = ASCII_DELIMITER;
    *current_pos = '\0';
    
    return lines;
}
