#include <ascii-chat/video/ascii/frame_validator.h>
#include <string.h>
#include <ascii-chat/logging.h>

/**
 * @brief Find the final reset sequence in frame data
 *
 * Searches from right to left to find the LAST occurrence of ESC[0m.
 * This handles cases where ESC[0m might appear in the middle of color codes.
 *
 * @param frame_data Frame buffer
 * @param frame_size Buffer size
 * @return Position of final ESC[0m, or SIZE_MAX if not found
 */
static size_t frame_find_final_reset(const char *frame_data, size_t frame_size) {
  if (!frame_data || frame_size < 4) {
    return SIZE_MAX;
  }

  const char *reset_seq = "\033[0m";
  size_t reset_len = 4;

  // Search backwards from end to find LAST reset sequence
  for (int i = (int)frame_size - (int)reset_len; i >= 0; i--) {
    if (memcmp(&frame_data[i], reset_seq, reset_len) == 0) {
      return (size_t)i;
    }
  }

  return SIZE_MAX;
}

bool frame_validate_integrity(const char *frame_data, size_t frame_size) {
  if (!frame_data || frame_size == 0) {
    return false;
  }

  size_t reset_pos = frame_find_final_reset(frame_data, frame_size);
  if (reset_pos == SIZE_MAX) {
    // No reset sequence found - this is suspicious
    log_warn("FRAME_VALIDATION: No ESC[0m reset sequence found in %zu byte frame", frame_size);
    return false;
  }

  size_t expected_end = reset_pos + 4; // 4 bytes for ESC[0m

  // Check if there's garbage after the reset
  if (expected_end < frame_size) {
    // Calculate how much garbage we have
    size_t garbage_size = frame_size - expected_end;

    // Check what the garbage looks like
    const uint8_t *garbage = (const uint8_t *)&frame_data[expected_end];

    // Log the garbage for debugging
    log_warn("FRAME_VALIDATION: Found %zu garbage bytes after reset at offset %zu (frame size %zu): "
             "[%02x %02x %02x %02x ...]",
             garbage_size, expected_end, frame_size, garbage[0], garbage_size > 1 ? garbage[1] : 0,
             garbage_size > 2 ? garbage[2] : 0, garbage_size > 3 ? garbage[3] : 0);

    return false;
  }

  // Frame looks valid
  log_debug("FRAME_VALIDATION: Frame OK (%zu bytes, ends at offset %zu)", frame_size, expected_end);
  return true;
}

size_t frame_get_valid_end(const char *frame_data, size_t frame_size) {
  if (!frame_data || frame_size < 4) {
    return frame_size;
  }

  size_t reset_pos = frame_find_final_reset(frame_data, frame_size);
  if (reset_pos == SIZE_MAX) {
    return frame_size; // No reset found, assume all data is valid
  }

  return reset_pos + 4; // Return position after the reset sequence
}
