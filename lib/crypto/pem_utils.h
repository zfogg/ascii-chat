/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 * Adapted for ASCII-Chat by Zachary Fogg <me@zfo.gg>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

/**
 * @file pem_utils.h
 * @brief BearSSL PEM and trust anchor utilities adapted for in-memory data
 *
 * This file contains BearSSL tools utilities adapted to work with in-memory
 * PEM data instead of files. Original code from BearSSL tools (ISC license).
 *
 * @author Thomas Pornin (original BearSSL tools)
 * @author Zachary Fogg <me@zfo.gg> (adaptation for ASCII-Chat)
 * @date October 2025
 */

#include <bearssl.h>
#include <stddef.h>

/**
 * Vector type for trust anchors
 *
 * This is a dynamic array of trust anchors, used to accumulate
 * root CA certificates for TLS validation.
 */
typedef struct {
  br_x509_trust_anchor *buf;
  size_t ptr;
  size_t len;
} anchor_list;

/**
 * Initializer for anchor_list
 */
#define ANCHOR_LIST_INIT {NULL, 0, 0}

/**
 * Read trust anchors from PEM-encoded data in memory
 *
 * Parses PEM-encoded CA certificates from a memory buffer and converts them
 * to BearSSL trust anchors. The trust anchors are appended to the provided
 * anchor_list.
 *
 * @param dst Pointer to anchor_list to append trust anchors to
 * @param pem_data Pointer to PEM-encoded certificate data
 * @param pem_len Length of PEM data in bytes
 * @return Number of trust anchors successfully decoded and added, or 0 on
 * error
 *
 * Example:
 *   anchor_list anchors = ANCHOR_LIST_INIT;
 *   char *pem_data = ...;
 *   size_t pem_len = strlen(pem_data);
 *   size_t num = read_trust_anchors_from_memory(&anchors, pem_data, pem_len);
 *   if (num > 0) {
 *       // Use anchors.buf with BearSSL
 *       // ...
 *       // Free trust anchor contents when done
 *       for (size_t i = 0; i < anchors.ptr; i++) {
 *           free_ta_contents(&anchors.buf[i]);
 *       }
 *       SAFE_FREE(anchors.buf);
 *   }
 */
size_t read_trust_anchors_from_memory(anchor_list *dst, const unsigned char *pem_data, size_t pem_len);

/**
 * Free the contents of a trust anchor
 *
 * Releases all dynamically allocated memory within a trust anchor structure,
 * but does not free the structure itself. Call this for each trust anchor
 * before freeing the anchor_list buffer.
 *
 * @param ta Pointer to trust anchor to free
 */
void free_ta_contents(br_x509_trust_anchor *ta);
