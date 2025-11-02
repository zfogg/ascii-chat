#pragma once

#include "../common.h"

/**
 * @file parsing.h
 * @brief Safe parsing utilities
 *
 * Provides utilities for parsing strings to various data types with validation.
 */

/**
 * Parse SIZE message format
 *
 * Parses "SIZE:width,height" format with validation.
 *
 * @param message Input message
 * @param width Output width
 * @param height Output height
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t safe_parse_size_message(const char *message, unsigned int *width, unsigned int *height);

/**
 * Parse AUDIO message format
 *
 * Parses "AUDIO:num_samples" format with validation.
 *
 * @param message Input message
 * @param num_samples Output sample count
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t safe_parse_audio_message(const char *message, unsigned int *num_samples);
