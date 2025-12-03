/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 * Adapted for ascii-chat by Zachary Fogg <me@zfo.gg>
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
 * @file crypto/pem_utils.h
 * @brief BearSSL PEM and trust anchor utilities adapted for in-memory data
 * @ingroup crypto
 * @addtogroup crypto
 * @{
 *
 * This file contains BearSSL tools utilities adapted to work with in-memory
 * PEM data instead of files. Original code from BearSSL tools (ISC license).
 *
 * These utilities are used for loading system CA certificates for TLS validation.
 *
 * @note BearSSL adaptation: Original BearSSL tools read PEM data from files.
 *       This version works with in-memory PEM data for better flexibility.
 *
 * @note Trust anchors: Trust anchors are root CA certificates used for TLS validation.
 *       Loaded from system CA certificate store and used to validate server certificates.
 *
 * @note Memory management: Trust anchors contain dynamically allocated memory.
 *       Must call free_ta_contents() for each anchor and free anchor_list.buf.
 *
 * @author Thomas Pornin (original BearSSL tools)
 * @author Zachary Fogg <me@zfo.gg> (adaptation for ascii-chat)
 * @date October 2025
 */

#include <bearssl.h>
#include <stddef.h>

/**
 * @name Trust Anchor Management
 * @{
 */

/**
 * @brief Vector type for trust anchors
 *
 * Dynamic array of trust anchors, used to accumulate root CA certificates
 * for TLS validation. Trust anchors are loaded from system CA certificate store.
 *
 * @note Structure fields:
 *       - buf: Pointer to array of trust anchors (dynamically allocated)
 *       - ptr: Current number of trust anchors in array
 *       - len: Total capacity of array (may be larger than ptr)
 *
 * @note Memory management: buf must be allocated with malloc() and freed with free().
 *       Each trust anchor must be freed with free_ta_contents() before freeing buf.
 *
 * @ingroup crypto
 */
typedef struct {
  br_x509_trust_anchor *buf; /**< Array of trust anchors (dynamically allocated) */
  size_t ptr;                /**< Current number of trust anchors */
  size_t len;                /**< Total capacity of array */
} anchor_list;

/**
 * @brief Initializer for anchor_list
 *
 * Macro for initializing an empty anchor_list structure.
 * Sets all fields to zero/NULL.
 *
 * @ingroup crypto
 */
// clang-format off
#define ANCHOR_LIST_INIT {NULL, 0, 0}
// clang-format on

/**
 * @brief Read trust anchors from PEM-encoded data in memory
 * @param dst Pointer to anchor_list to append trust anchors to (must not be NULL)
 * @param pem_data Pointer to PEM-encoded certificate data (must not be NULL)
 * @param pem_len Length of PEM data in bytes (must be > 0)
 * @return Number of trust anchors successfully decoded and added, or 0 on error
 *
 * Parses PEM-encoded CA certificates from a memory buffer and converts them
 * to BearSSL trust anchors. The trust anchors are appended to the provided
 * anchor_list.
 *
 * @note PEM format: Accepts PEM-encoded certificates (-----BEGIN CERTIFICATE-----).
 *       Supports multiple certificates in single PEM data (concatenated).
 *
 * @note Trust anchor parsing: Parses each certificate in PEM data and creates
 *       trust anchor structure. Appends trust anchors to anchor_list.
 *
 * @note Memory allocation: Dynamically allocates memory for trust anchors.
 *       Resizes anchor_list.buf as needed (realloc).
 *
 * @note Error handling: Returns 0 on error (parse error, memory error, etc.).
 *       Partially parsed trust anchors are retained (not rolled back).
 *
 * @warning Memory management: Trust anchors contain dynamically allocated memory.
 *          Must call free_ta_contents() for each anchor and free anchor_list.buf.
 *
 * @warning PEM data format: PEM data must be valid PEM-encoded certificates.
 *          Invalid PEM data may cause parse errors or crashes.
 *
 * @ingroup crypto
 */
size_t read_trust_anchors_from_memory(anchor_list *dst, const unsigned char *pem_data, size_t pem_len);

/**
 * @brief Free the contents of a trust anchor
 * @param ta Pointer to trust anchor to free (must not be NULL)
 *
 * Releases all dynamically allocated memory within a trust anchor structure,
 * but does not free the structure itself. Call this for each trust anchor
 * before freeing the anchor_list buffer.
 *
 * @note Memory cleanup: Frees certificate data, public key, and other
 *       dynamically allocated fields within trust anchor structure.
 *
 * @note Structure preservation: Does not free the trust anchor structure itself.
 *       Only frees dynamically allocated fields within the structure.
 *
 * @note Usage: Must be called for each trust anchor in anchor_list.buf before
 *       freeing anchor_list.buf. Loop through anchors and call for each one.
 *
 * @warning Memory leak: Must call this for each trust anchor before freeing
 *          anchor_list.buf. Failing to do so will leak memory.
 *
 * @ingroup crypto
 */
void free_ta_contents(br_x509_trust_anchor *ta);

/** @} */
