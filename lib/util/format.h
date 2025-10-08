#pragma once

#include <stddef.h>

/**
 * @file format.h
 * @brief String formatting utilities
 *
 * Provides utilities for formatting various data types into human-readable strings.
 */

/**
 * Format byte count into human-readable string
 *
 * Formats as: "512 B", "1.50 MB", "2.34 GB", etc.
 *
 * @param bytes Number of bytes to format
 * @param out Output buffer
 * @param out_capacity Size of output buffer
 */
void format_bytes_pretty(size_t bytes, char *out, size_t out_capacity);
