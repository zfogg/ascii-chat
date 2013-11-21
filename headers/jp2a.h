/*
 * Copyright (C) 2006 Christian Stigen Larsen, http://csl.sublevel3.org
 * Distributed under the GNU General Public License (GPL) v2.
 *
 * Project homepage on http://jp2a.sf.net
 *
 * $Id: jp2a.h 439 2006-09-01 10:52:47Z csl $
 */

int ascii_init(int argc, char** argv);

// image.c
void decompress(FILE *fin, FILE *fout);

// options.c
void parse_options(int argc, char** argv);
