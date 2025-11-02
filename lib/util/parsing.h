#pragma once

/**
 * @file util/parsing.h
 * @ingroup util
 * @brief Safe Parsing Utilities
 *
 * This header provides safe, validated parsing utilities for protocol message
 * formats. Functions validate input strings, check for overflow conditions,
 * and return structured error codes on failure.
 *
 * CORE FEATURES:
 * ==============
 * - Protocol message format parsing (SIZE, AUDIO)
 * - Input validation and sanitization
 * - Overflow protection for numeric values
 * - Structured error reporting (asciichat_error_t)
 * - Safe parsing without undefined behavior
 *
 * PROTOCOL MESSAGE FORMATS:
 * =========================
 * The system supports parsing of ASCII-Chat protocol messages:
 * - SIZE: "SIZE:width,height" - Video frame dimensions
 * - AUDIO: "AUDIO:num_samples" - Audio sample count
 *
 * VALIDATION:
 * ===========
 * All parsing functions validate:
 * - Message format correctness
 * - Numeric value ranges (no overflow)
 * - Required fields presence
 * - Delimiter correctness
 *
 * @note All parsing functions are safe and do not cause undefined behavior.
 * @note Invalid input returns error codes (ASCIICHAT_ERROR_*) for proper handling.
 * @note Output parameters are only modified on successful parsing.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include "../common.h"

/* ============================================================================
 * Protocol Message Parsing Functions
 * @{
 */

/**
 * @brief Parse SIZE message format
 * @param message Input message string (must not be NULL, format: "SIZE:width,height")
 * @param width Output parameter for parsed width (must not be NULL)
 * @param height Output parameter for parsed height (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses a SIZE protocol message in the format "SIZE:width,height" where width
 * and height are unsigned integers. Validates the message format, parses numeric
 * values, and checks for overflow conditions.
 *
 * MESSAGE FORMAT:
 * - Must start with "SIZE:"
 * - Width and height separated by comma
 * - Both values must be valid unsigned integers
 * - No whitespace allowed in values
 *
 * @note Width and height are only modified on successful parsing.
 * @note Returns ASCIICHAT_ERROR_INVALID_ARGUMENT if format is invalid.
 * @note Returns ASCIICHAT_ERROR_OVERFLOW if numeric values exceed limits.
 *
 * @par Example
 * @code
 * unsigned int width, height;
 * if (safe_parse_size_message("SIZE:1920,1080", &width, &height) == ASCIICHAT_OK) {
 *     // width = 1920, height = 1080
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t safe_parse_size_message(const char *message, unsigned int *width, unsigned int *height);

/**
 * @brief Parse AUDIO message format
 * @param message Input message string (must not be NULL, format: "AUDIO:num_samples")
 * @param num_samples Output parameter for parsed sample count (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses an AUDIO protocol message in the format "AUDIO:num_samples" where
 * num_samples is an unsigned integer. Validates the message format, parses
 * the numeric value, and checks for overflow conditions.
 *
 * MESSAGE FORMAT:
 * - Must start with "AUDIO:"
 * - Sample count must be a valid unsigned integer
 * - No whitespace allowed in value
 *
 * @note num_samples is only modified on successful parsing.
 * @note Returns ASCIICHAT_ERROR_INVALID_ARGUMENT if format is invalid.
 * @note Returns ASCIICHAT_ERROR_OVERFLOW if numeric value exceeds limits.
 *
 * @par Example
 * @code
 * unsigned int num_samples;
 * if (safe_parse_audio_message("AUDIO:44100", &num_samples) == ASCIICHAT_OK) {
 *     // num_samples = 44100
 * }
 * @endcode
 *
 * @ingroup util
 */
asciichat_error_t safe_parse_audio_message(const char *message, unsigned int *num_samples);

/** @} */
