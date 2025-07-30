/*
 * Copyright (C) 2006 Christian Stigen Larsen, http://csl.sublevel3.org
 * Distributed under the GNU General Public License (GPL) v2.
 *
 * Project homepage on http://jp2a.sf.net
 *
 * $Id: image.h 470 2006-10-12 08:13:37Z cslsublevel3org $
 */

#include <jpeglib.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct rgb_t {
  JSAMPLE r, g, b;
} rgb_t;

typedef struct image_t {
  int w, h;
  rgb_t *pixels;
} image_t;

image_t *image_read(FILE *);
image_t *image_new(int, int);
void image_destroy(image_t *);
void image_clear(image_t *);
char *image_print(const image_t *);
char *image_print_colored(const image_t *);
void quantize_color(int *r, int *g, int *b, int levels);
void image_resize(const image_t *, image_t *);
