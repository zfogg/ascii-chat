/*
 * Copyright (C) 2006 Christian Stigen Larsen, http://csl.sublevel3.org
 * Distributed under the GNU General Public License (GPL) v2.
 *
 * Project homepage on http://jp2a.sf.net
 *
 * $Id: aspect_ratio.c 466 2006-10-02 11:35:03Z csl $
 */
#include <sys/types.h>

void aspect_ratio(const int jpeg_width, const int jpeg_height, ssize_t *out_width, ssize_t *out_height);
