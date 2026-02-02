/**
 * @defgroup protocol_constants Protocol Constants
 * @ingroup module_core
 * @brief Protocol version, feature flags, compression, and frame constants
 *
 * @file protocol_constants.h
 * @brief Protocol version, features, compression, and frame flag constants
 * @ingroup protocol_constants
 * @addtogroup protocol_constants
 * @{
 */

#pragma once

/* ============================================================================
 * Protocol Version Constants
 * ============================================================================ */

/** @brief Major protocol version number */
#define PROTOCOL_VERSION_MAJOR 1

/** @brief Minor protocol version number */
#define PROTOCOL_VERSION_MINOR 0

/* ============================================================================
 * Feature Flags
 * ============================================================================ */

/** @brief Run-length encoding support flag */
#define FEATURE_RLE_ENCODING 0x01

/** @brief Delta frame encoding support flag */
#define FEATURE_DELTA_FRAMES 0x02

/* ============================================================================
 * Compression Constants
 * ============================================================================ */

/** @brief No compression algorithm */
#define COMPRESS_ALGO_NONE 0x00

/** @brief zlib deflate compression algorithm */
#define COMPRESS_ALGO_ZLIB 0x01

/** @brief LZ4 fast compression algorithm */
#define COMPRESS_ALGO_LZ4 0x02

/** @brief zstd algorithm */
#define COMPRESS_ALGO_ZSTD 0x03

/* ============================================================================
 * Frame Flags
 * ============================================================================ */

/** @brief Frame includes ANSI color codes */
#define FRAME_FLAG_HAS_COLOR 0x01

/** @brief Frame data is compressed */
#define FRAME_FLAG_IS_COMPRESSED 0x02

/** @brief Frame data is RLE compressed */
#define FRAME_FLAG_RLE_COMPRESSED 0x04

/** @brief Frame was stretched (aspect adjusted) */
#define FRAME_FLAG_IS_STRETCHED 0x08

/* ============================================================================
 * Pixel Format Constants
 * ============================================================================ */

/** @brief RGB pixel format */
#define PIXEL_FORMAT_RGB 0

/** @brief RGBA pixel format */
#define PIXEL_FORMAT_RGBA 1

/** @brief BGR pixel format */
#define PIXEL_FORMAT_BGR 2

/** @brief BGRA pixel format */
#define PIXEL_FORMAT_BGRA 3

/** @} */
