#pragma once

/*
 * Copyright (C) 2006 Christian Stigen Larsen, http://csl.sublevel3.org
 * Distributed under the GNU General Public License (GPL) v2.
 *
 * Project homepage on http://jp2a.sf.net
 *
 * $Id: aspect_ratio.c 466 2006-10-02 11:35:03Z csl $
 */
#include "platform.h"
#include <stdbool.h>

void aspect_ratio(const ssize_t img_width, const ssize_t img_height, const ssize_t width, const ssize_t height,
                  const bool stretch, ssize_t *out_width, ssize_t *out_height);

// Simple aspect ratio calculation without terminal character correction
void aspect_ratio2(const ssize_t img_width, const ssize_t img_height, const ssize_t target_width,
                   const ssize_t target_height, ssize_t *out_width, ssize_t *out_height);

void calculate_fit_dimensions_pixel(int img_width, int img_height, int max_width, int max_height, int *out_width,
                                    int *out_height);
