#pragma once

/**
 * @file crc32.h
 * @ingroup util
 * @brief Hardware-Accelerated CRC32 Checksum Computation
 *
 * This header provides CRC32 checksum computation with automatic hardware
 * acceleration when available. The system automatically dispatches to hardware
 * implementations (SSE4.2, ARM CRC32) for optimal performance, falling back to
 * a software implementation on platforms without hardware support.
 *
 * CORE FEATURES:
 * ==============
 * - Automatic hardware acceleration detection
 * - Hardware-accelerated CRC32 on supported platforms (SSE4.2, ARM)
 * - Software fallback implementation (always available)
 * - Transparent performance optimization
 * - Used for network packet integrity verification
 *
 * HARDWARE ACCELERATION:
 * ======================
 * The system supports:
 * - SSE4.2 CRC32 instruction (x86/x64 processors)
 * - ARM CRC32 instructions (ARMv8 and later)
 * - Automatic runtime detection and dispatch
 * - Significant performance improvement over software implementation
 *
 * SOFTWARE FALLBACK:
 * ==================
 * - Always available on all platforms
 * - Polynomial: IEEE 802.3 (CRC-32)
 * - Optimized lookup table implementation
 * - Reliable for packet integrity verification
 *
 * PERFORMANCE:
 * =============
 * Hardware acceleration provides:
 * - 10-100x faster than software implementation
 * - Zero overhead when hardware is available
 * - Automatic fallback ensures compatibility
 *
 * @note Use the asciichat_crc32() macro for automatic hardware dispatch.
 * @note Hardware acceleration is detected at runtime, so no compile-time
 *       configuration is required.
 * @note CRC32 is used primarily for network packet integrity verification
 *       to detect transmission errors.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Compute CRC32 checksum with hardware acceleration (if available)
 * @param data Data to compute checksum for (must not be NULL)
 * @param len Length of data in bytes
 * @return CRC32 checksum value (32-bit unsigned integer)
 *
 * Computes CRC32 checksum using hardware acceleration when available.
 * Automatically falls back to software implementation if hardware is
 * not available on the current platform.
 *
 * @note This function performs runtime CPU feature detection to determine
 *       whether to use hardware or software implementation.
 *
 * @note Hardware acceleration is available on:
 *       - x86/x64 processors with SSE4.2 (CRC32 instruction)
 *       - ARM processors with ARMv8 (CRC32 instructions)
 *
 * @note For consistent performance testing or when hardware detection is
 *       not desired, use asciichat_crc32_sw() directly.
 *
 * @note The CRC32 polynomial used is IEEE 802.3 (CRC-32), which is
 *       compatible with standard CRC32 implementations.
 */
uint32_t asciichat_crc32_hw(const void *data, size_t len);

/**
 * @brief Compute CRC32 checksum using software implementation only
 * @param data Data to compute checksum for (must not be NULL)
 * @param len Length of data in bytes
 * @return CRC32 checksum value (32-bit unsigned integer)
 *
 * Computes CRC32 checksum using a software lookup table implementation.
 * Always uses software implementation regardless of hardware availability.
 *
 * @note This function provides consistent performance across all platforms,
 *       which is useful for testing and benchmarking.
 *
 * @note For production code, prefer asciichat_crc32() macro which
 *       automatically uses hardware acceleration when available.
 *
 * @note The CRC32 polynomial used is IEEE 802.3 (CRC-32), which is
 *       compatible with standard CRC32 implementations.
 */
uint32_t asciichat_crc32_sw(const void *data, size_t len);

/**
 * @brief Check if hardware CRC32 acceleration is available at runtime
 * @return true if hardware acceleration is available, false otherwise
 *
 * Performs runtime CPU feature detection to determine if hardware
 * CRC32 acceleration can be used on the current platform.
 *
 * @note Detection is performed once per program execution and cached.
 *       Subsequent calls return the cached result.
 *
 * @note Hardware acceleration is available on:
 *       - x86/x64: Processors with SSE4.2 feature (CRC32 instruction)
 *       - ARM: Processors with ARMv8 architecture (CRC32 instructions)
 *
 * @note Use this function to determine if hardware acceleration will
 *       be used before calling asciichat_crc32_hw().
 */
bool crc32_hw_is_available(void);

/**
 * @brief Main CRC32 dispatcher macro - use this in application code
 * @param data Data to compute checksum for
 * @param len Length of data in bytes
 * @return CRC32 checksum value (32-bit unsigned integer)
 *
 * Recommended macro for CRC32 computation. Automatically dispatches to
 * hardware-accelerated implementation when available, otherwise uses
 * software fallback.
 *
 * @note This macro expands to asciichat_crc32_hw() which performs
 *       automatic hardware detection and fallback.
 *
 * @note Use this macro in all application code for optimal performance
 *       with automatic hardware acceleration.
 *
 * @par Example:
 * @code{.c}
 * uint32_t checksum = asciichat_crc32(packet_data, packet_size);
 * @endcode
 */
#define asciichat_crc32(data, len) asciichat_crc32_hw((data), (len))
