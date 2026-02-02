#ifndef FRAME_VALIDATOR_H
#define FRAME_VALIDATOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Validate ASCII frame integrity
 *
 * Checks for common corruption patterns:
 * - Frame ends with proper reset sequence ESC[0m
 * - No garbage bytes after reset sequence
 * - Frame is properly null-terminated
 *
 * @param frame_data Frame buffer (may not be null-terminated)
 * @param frame_size Actual frame size in bytes
 * @return true if frame appears valid, false if corruption detected
 */
bool frame_validate_integrity(const char *frame_data, size_t frame_size);

/**
 * @brief Get the actual valid frame end position
 *
 * Finds where the frame SHOULD end (right after ESC[0m reset sequence).
 * Returns the position after the final reset, or frame_size if no reset found.
 *
 * @param frame_data Frame buffer
 * @param frame_size Buffer size
 * @return Position where frame should end
 */
size_t frame_get_valid_end(const char *frame_data, size_t frame_size);

#endif // FRAME_VALIDATOR_H
