/**
 * @file util/magic.h
 * @brief Magic number validation constants and macros
 * @ingroup util
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Magic Number Definitions
 * ============================================================================ */

/** @brief Magic number for valid ring buffer frames (0xA5C11C4A1 = "ASCIICHAT" in hex) */
#define MAGIC_FRAME_VALID 0xA5C11C4A1ULL

/** @brief Magic number for freed ring buffer frames (0xFEEDFACE) */
#define MAGIC_FRAME_FREED 0xFEEDFACEU

/** @brief Magic number for valid pooled buffers (0xBF00B001) */
#define MAGIC_BUFFER_POOL_VALID 0xBF00B001U

/** @brief Magic number for malloc fallback buffers (0xBF00FA11) */
#define MAGIC_BUFFER_POOL_FALLBACK 0xBF00FA11U

/** @brief Magic number for network packets (0xA5C11C4A1 = "ASCIICHAT" in hex) */
#define MAGIC_PACKET_VALID 0xA5C11C4A1ULL

/* ============================================================================
 * Validation Macros
 * ============================================================================ */

/** @brief Check if magic number is valid (true if matches expected) */
#define IS_MAGIC_VALID(magic, expected) ((magic) == (expected))

/** @brief Check if frame is valid and not freed */
#define IS_FRAME_VALID(frame) ((frame)->magic == MAGIC_FRAME_VALID && (frame)->data != NULL)

/** @brief Check if frame has been freed (corruption detection) */
#define IS_FRAME_FREED(frame) ((frame)->magic == MAGIC_FRAME_FREED)

/** @brief Check if buffer pool node is valid */
#define IS_BUFFER_POOL_VALID(node)                                                                                     \
  ((node)->magic == MAGIC_BUFFER_POOL_VALID || (node)->magic == MAGIC_BUFFER_POOL_FALLBACK)

/** @brief Mark a frame as freed */
#define MARK_FRAME_FREED(frame) ((frame)->magic = MAGIC_FRAME_FREED)

/** @} */
