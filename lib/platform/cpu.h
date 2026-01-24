#pragma once

/**
 * @file platform/cpu.h
 * @brief Cross-platform CPU feature detection
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent functions to detect CPU capabilities at runtime.
 * Includes detection for:
 * - SIMD instruction sets (SSE4.2, AVX, AVX2, NEON)
 * - Hardware acceleration (CRC32, AES-NI)
 *
 * Detection is performed lazily on first call and cached for subsequent calls.
 * Thread-safe with atomic synchronization.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if CPU supports CRC32 hardware acceleration
 *
 * Detects support for hardware CRC32 instructions:
 * - x86-64: SSE4.2 CRC32 instruction
 * - ARM64: ARMv8 CRC32 instruction
 *
 * Platform-specific implementations:
 *   - Windows x86-64: Uses CPUID instruction via __cpuid()
 *   - POSIX x86-64: Uses __get_cpuid() from cpuid.h
 *   - Apple Silicon: CRC32 always available
 *   - Other ARM64: Assumes available (can be extended with hwcap detection)
 *   - Other architectures: Returns false
 *
 * Detection is performed once on first call and cached for thread-safe reuse.
 *
 * @return true if CPU supports CRC32 hardware acceleration, false otherwise
 *
 * @note Result is cached after first call (thread-safe)
 * @note Safe to call from multiple threads
 *
 * @par Example:
 * @code
 * if (cpu_has_crc32()) {
 *     // Use hardware CRC32 instruction
 * } else {
 *     // Use software fallback
 * }
 * @endcode
 *
 * @ingroup platform
 */
bool cpu_has_crc32(void);

/**
 * @brief Check if CPU supports SSE4.2 (includes CRC32 on x86-64)
 *
 * Detects support for SSE4.2 instruction set, which includes CRC32 on x86-64.
 *
 * @return true if CPU supports SSE4.2, false otherwise
 *
 * @note Detection is cached after first call (thread-safe)
 * @note On non-x86-64 platforms, returns false
 *
 * @ingroup platform
 */
bool cpu_has_sse42(void);

/**
 * @brief Check if CPU supports AVX2
 *
 * Detects support for AVX2 instruction set for vector operations.
 *
 * @return true if CPU supports AVX2, false otherwise
 *
 * @note Detection is cached after first call (thread-safe)
 * @note On non-x86-64 platforms, returns false
 *
 * @ingroup platform
 */
bool cpu_has_avx2(void);

/**
 * @brief Check if CPU supports NEON (ARM SIMD)
 *
 * Detects support for ARM NEON instruction set.
 *
 * @return true if CPU supports NEON, false otherwise
 *
 * @note Detection is cached after first call (thread-safe)
 * @note On non-ARM platforms, returns false
 * @note On ARM platforms, returns true (NEON support is expected)
 *
 * @ingroup platform
 */
bool cpu_has_neon(void);

#ifdef __cplusplus
}
#endif

/** @} */
