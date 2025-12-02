# =============================================================================
# CPUDetection.cmake - Unified CPU feature detection
# =============================================================================
# Provides centralized CPU architecture and feature detection for use by
# SIMD.cmake, CRC32.cmake, and CompilerFlags.cmake.
#
# Sets:
#   ASCIICHAT_IS_X86_64 - TRUE if targeting x86_64/AMD64
#   ASCIICHAT_IS_ARM64  - TRUE if targeting ARM64/aarch64
#   ASCIICHAT_IS_ARM32  - TRUE if targeting ARM32
#
# Provides functions:
#   detect_x86_64_level(RESULT_VAR) - Detects x86-64-v2/v3 microarchitecture level
#   detect_x86_simd_features() - Detects SSE2/SSSE3/AVX2 support, sets HAS_* vars
#   detect_x86_sse42() - Detects SSE4.2 support, sets HAS_SSE42
#   detect_arm_neon() - Detects NEON support, sets HAS_NEON
#   detect_arm_sve() - Detects SVE support, sets HAS_SVE
#   detect_arm_crc32() - Detects ARM CRC32 support, sets HAS_ARM_CRC32
#
# Must be included AFTER platform detection (PLATFORM_DARWIN, PLATFORM_LINUX, etc.)
# =============================================================================

include(CheckCSourceCompiles)
include(CheckCSourceRuns)

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_CPU_DETECTION_INCLUDED)
    return()
endif()
set(_ASCIICHAT_CPU_DETECTION_INCLUDED TRUE)

# =============================================================================
# Architecture Detection
# =============================================================================
# Detect processor architecture once and cache the results

if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|X86_64")
    set(ASCIICHAT_IS_X86_64 TRUE CACHE BOOL "Target is x86_64" FORCE)
    set(ASCIICHAT_IS_ARM64 FALSE CACHE BOOL "Target is ARM64" FORCE)
    set(ASCIICHAT_IS_ARM32 FALSE CACHE BOOL "Target is ARM32" FORCE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(ASCIICHAT_IS_X86_64 FALSE CACHE BOOL "Target is x86_64" FORCE)
    set(ASCIICHAT_IS_ARM64 TRUE CACHE BOOL "Target is ARM64" FORCE)
    set(ASCIICHAT_IS_ARM32 FALSE CACHE BOOL "Target is ARM32" FORCE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    set(ASCIICHAT_IS_X86_64 FALSE CACHE BOOL "Target is x86_64" FORCE)
    set(ASCIICHAT_IS_ARM64 FALSE CACHE BOOL "Target is ARM64" FORCE)
    set(ASCIICHAT_IS_ARM32 TRUE CACHE BOOL "Target is ARM32" FORCE)
else()
    set(ASCIICHAT_IS_X86_64 FALSE CACHE BOOL "Target is x86_64" FORCE)
    set(ASCIICHAT_IS_ARM64 FALSE CACHE BOOL "Target is ARM64" FORCE)
    set(ASCIICHAT_IS_ARM32 FALSE CACHE BOOL "Target is ARM32" FORCE)
endif()

mark_as_advanced(ASCIICHAT_IS_X86_64 ASCIICHAT_IS_ARM64 ASCIICHAT_IS_ARM32)

# =============================================================================
# x86_64 SIMD Feature Detection
# =============================================================================
# Detects SSE2, SSSE3, AVX2 support on x86_64 platforms
# Sets: HAS_SSE2, HAS_SSSE3, HAS_AVX2 in parent scope
# =============================================================================
function(detect_x86_simd_features)
    set(HAS_SSE2 FALSE PARENT_SCOPE)
    set(HAS_SSSE3 FALSE PARENT_SCOPE)
    set(HAS_AVX2 FALSE PARENT_SCOPE)

    if(NOT ASCIICHAT_IS_X86_64)
        return()
    endif()

    if(WIN32)
        # Windows x86_64 detection
        if(NOT CMAKE_CROSSCOMPILING AND NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))
            # Runtime detection using CPUID
            check_c_source_runs("
                #include <intrin.h>
                int main() {
                    int info[4];
                    __cpuidex(info, 7, 0);
                    if (info[1] & (1 << 5)) return 0; // AVX2
                    return 1;
                }
            " _HAS_AVX2_RUNTIME)

            if(_HAS_AVX2_RUNTIME)
                set(HAS_AVX2 TRUE PARENT_SCOPE)
                return()
            endif()

            check_c_source_runs("
                #include <intrin.h>
                int main() {
                    int info[4];
                    __cpuid(info, 1);
                    if (info[2] & (1 << 9)) return 0; // SSSE3
                    return 1;
                }
            " _HAS_SSSE3_RUNTIME)

            if(_HAS_SSSE3_RUNTIME)
                set(HAS_SSSE3 TRUE PARENT_SCOPE)
                return()
            endif()

            check_c_source_runs("
                #include <intrin.h>
                int main() {
                    int info[4];
                    __cpuid(info, 1);
                    if (info[3] & (1 << 26)) return 0; // SSE2
                    return 1;
                }
            " _HAS_SSE2_RUNTIME)

            if(_HAS_SSE2_RUNTIME)
                set(HAS_SSE2 TRUE PARENT_SCOPE)
            endif()
        else()
            # Cross-compiling or Clang on Windows - use compile-time checks
            set(CMAKE_REQUIRED_FLAGS_SAVE ${CMAKE_REQUIRED_FLAGS})

            set(CMAKE_REQUIRED_FLAGS "-mavx2")
            check_c_source_compiles("
                #include <immintrin.h>
                int main() { __m256i a = _mm256_setzero_si256(); return 0; }
            " _CAN_COMPILE_AVX2)

            if(_CAN_COMPILE_AVX2)
                set(HAS_AVX2 TRUE PARENT_SCOPE)
                set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
                return()
            endif()

            set(CMAKE_REQUIRED_FLAGS "-mssse3")
            check_c_source_compiles("
                #include <tmmintrin.h>
                int main() { __m128i a = _mm_setzero_si128(); a = _mm_abs_epi8(a); return 0; }
            " _CAN_COMPILE_SSSE3)

            if(_CAN_COMPILE_SSSE3)
                set(HAS_SSSE3 TRUE PARENT_SCOPE)
                set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
                return()
            endif()

            set(CMAKE_REQUIRED_FLAGS "-msse2")
            check_c_source_compiles("
                #include <emmintrin.h>
                int main() { __m128i a = _mm_setzero_si128(); return 0; }
            " _CAN_COMPILE_SSE2)

            set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})

            if(_CAN_COMPILE_SSE2)
                set(HAS_SSE2 TRUE PARENT_SCOPE)
            endif()
        endif()

    elseif(PLATFORM_DARWIN)
        # macOS detection using sysctl
        # Note: Check Rosetta first because IS_ROSETTA=1 implies IS_APPLE_SILICON=1
        # (Rosetta runs x86_64 code on Apple Silicon hardware)
        if(IS_ROSETTA EQUAL 1)
            # Rosetta - SSSE3 is safe
            set(HAS_SSSE3 TRUE PARENT_SCOPE)
        elseif(IS_APPLE_SILICON EQUAL 1)
            # Apple Silicon native - no x86 SIMD
            return()
        else()
            # Intel Mac - check for AVX2
            execute_process(
                COMMAND sysctl -n hw.optional.avx2_0
                OUTPUT_VARIABLE _HAS_AVX2_MAC
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_HAS_AVX2_MAC EQUAL 1)
                set(HAS_AVX2 TRUE PARENT_SCOPE)
            else()
                set(HAS_SSSE3 TRUE PARENT_SCOPE)
            endif()
        endif()

    elseif(PLATFORM_LINUX)
        # Linux detection using /proc/cpuinfo
        if(EXISTS "/proc/cpuinfo")
            file(READ "/proc/cpuinfo" _CPUINFO_CONTENT)
            if(_CPUINFO_CONTENT MATCHES "avx2")
                set(HAS_AVX2 TRUE PARENT_SCOPE)
            elseif(_CPUINFO_CONTENT MATCHES "ssse3")
                set(HAS_SSSE3 TRUE PARENT_SCOPE)
            elseif(_CPUINFO_CONTENT MATCHES "sse2")
                set(HAS_SSE2 TRUE PARENT_SCOPE)
            endif()
        endif()
    endif()
endfunction()

# =============================================================================
# x86_64 SSE4.2 Detection (for CRC32)
# =============================================================================
# Sets: HAS_SSE42 in parent scope
# =============================================================================
function(detect_x86_sse42)
    set(HAS_SSE42 FALSE PARENT_SCOPE)

    if(NOT ASCIICHAT_IS_X86_64)
        return()
    endif()

    if(WIN32)
        if(NOT CMAKE_CROSSCOMPILING AND NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))
            check_c_source_runs("
                #include <intrin.h>
                int main() {
                    int info[4];
                    __cpuid(info, 1);
                    if (info[2] & (1 << 20)) return 0; // SSE4.2
                    return 1;
                }
            " _HAS_SSE42_RUNTIME)
            if(_HAS_SSE42_RUNTIME)
                set(HAS_SSE42 TRUE PARENT_SCOPE)
            endif()
        endif()

    elseif(PLATFORM_DARWIN)
        if(IS_APPLE_SILICON EQUAL 1)
            return()
        endif()
        execute_process(
            COMMAND sysctl -n hw.optional.sse4_2
            OUTPUT_VARIABLE _HAS_SSE42_MAC
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_HAS_SSE42_MAC EQUAL 1)
            set(HAS_SSE42 TRUE PARENT_SCOPE)
        endif()

    elseif(PLATFORM_LINUX)
        if(EXISTS "/proc/cpuinfo")
            file(READ "/proc/cpuinfo" _CPUINFO_CONTENT)
            if(_CPUINFO_CONTENT MATCHES "sse4_2")
                set(HAS_SSE42 TRUE PARENT_SCOPE)
            endif()
        endif()
    endif()
endfunction()

# =============================================================================
# ARM NEON Detection
# =============================================================================
# Sets: HAS_NEON in parent scope
# =============================================================================
function(detect_arm_neon)
    set(HAS_NEON FALSE PARENT_SCOPE)

    if(ASCIICHAT_IS_ARM64)
        # ARM64 always has NEON
        set(HAS_NEON TRUE PARENT_SCOPE)
        return()
    endif()

    if(NOT ASCIICHAT_IS_ARM32)
        return()
    endif()

    # ARM32 - NEON is optional
    if(WIN32)
        check_c_source_compiles("
            #include <arm_neon.h>
            int main() {
                uint8x16_t a = vdupq_n_u8(0);
                return 0;
            }
        " _CAN_COMPILE_NEON)
        if(_CAN_COMPILE_NEON)
            set(HAS_NEON TRUE PARENT_SCOPE)
        endif()

    elseif(PLATFORM_LINUX AND EXISTS "/proc/cpuinfo")
        file(READ "/proc/cpuinfo" _CPUINFO_CONTENT)
        if(_CPUINFO_CONTENT MATCHES "neon")
            set(HAS_NEON TRUE PARENT_SCOPE)
        endif()
    endif()
endfunction()

# =============================================================================
# ARM SVE Detection
# =============================================================================
# Sets: HAS_SVE in parent scope
# =============================================================================
function(detect_arm_sve)
    set(HAS_SVE FALSE PARENT_SCOPE)

    if(NOT ASCIICHAT_IS_ARM64)
        return()
    endif()

    if(PLATFORM_LINUX AND EXISTS "/proc/cpuinfo")
        file(READ "/proc/cpuinfo" _CPUINFO_CONTENT)
        if(_CPUINFO_CONTENT MATCHES "sve")
            set(HAS_SVE TRUE PARENT_SCOPE)
        endif()
    endif()
endfunction()

# =============================================================================
# ARM CRC32 Detection
# =============================================================================
# Sets: HAS_ARM_CRC32 in parent scope
# =============================================================================
function(detect_arm_crc32)
    set(HAS_ARM_CRC32 FALSE PARENT_SCOPE)

    if(NOT ASCIICHAT_IS_ARM64 AND NOT ASCIICHAT_IS_ARM32)
        return()
    endif()

    if(PLATFORM_DARWIN AND IS_APPLE_SILICON EQUAL 1)
        # Apple Silicon always has CRC32
        set(HAS_ARM_CRC32 TRUE PARENT_SCOPE)
        return()
    endif()

    if(WIN32)
        if(NOT CMAKE_CROSSCOMPILING)
            check_c_source_runs("
                #include <arm_acle.h>
                #include <stdint.h>
                int main() {
                    uint32_t crc = 0;
                    uint8_t data = 0x42;
                    crc = __crc32b(crc, data);
                    return 0;
                }
            " _HAS_ARM_CRC32_RUNTIME)
            if(_HAS_ARM_CRC32_RUNTIME)
                set(HAS_ARM_CRC32 TRUE PARENT_SCOPE)
            endif()
        else()
            set(CMAKE_REQUIRED_FLAGS "-march=armv8-a+crc")
            check_c_source_compiles("
                #include <arm_acle.h>
                #include <stdint.h>
                int main() {
                    uint32_t crc = 0;
                    uint8_t data = 0x42;
                    crc = __crc32b(crc, data);
                    return 0;
                }
            " _CAN_COMPILE_ARM_CRC32)
            unset(CMAKE_REQUIRED_FLAGS)
            if(_CAN_COMPILE_ARM_CRC32)
                set(HAS_ARM_CRC32 TRUE PARENT_SCOPE)
            endif()
        endif()

    elseif(PLATFORM_LINUX AND EXISTS "/proc/cpuinfo")
        file(READ "/proc/cpuinfo" _CPUINFO_CONTENT)
        if(_CPUINFO_CONTENT MATCHES "crc32")
            set(HAS_ARM_CRC32 TRUE PARENT_SCOPE)
        endif()

    else()
        # Fallback - try compile test
        set(CMAKE_REQUIRED_FLAGS "-march=armv8-a+crc")
        check_c_source_compiles("
            #include <arm_acle.h>
            #include <stdint.h>
            int main() {
                uint32_t crc = 0;
                uint8_t data = 0x42;
                crc = __crc32b(crc, data);
                return 0;
            }
        " _CAN_COMPILE_ARM_CRC32)
        unset(CMAKE_REQUIRED_FLAGS)
        if(_CAN_COMPILE_ARM_CRC32)
            set(HAS_ARM_CRC32 TRUE PARENT_SCOPE)
        endif()
    endif()
endfunction()

# =============================================================================
# x86-64 Microarchitecture Level Detection
# =============================================================================
# Detects x86-64-v2/v3 support for -march flag optimization
# Args:
#   DETECTED_LEVEL - Output variable for the detected level
# Returns: "x86-64-v3", "x86-64-v2", or "portable"
# =============================================================================
function(detect_x86_64_level DETECTED_LEVEL)
    set(${DETECTED_LEVEL} "portable" PARENT_SCOPE)

    if(NOT ASCIICHAT_IS_X86_64)
        return()
    endif()

    # Skip detection if cross-compiling (can't run test programs)
    if(CMAKE_CROSSCOMPILING)
        return()
    endif()

    # Check if compiler supports x86-64-v3 flag (GCC 11+, Clang 12+)
    set(CMAKE_REQUIRED_FLAGS_SAVE ${CMAKE_REQUIRED_FLAGS})
    check_c_compiler_flag("-march=x86-64-v3" _COMPILER_SUPPORTS_V3_FLAG)
    if(NOT _COMPILER_SUPPORTS_V3_FLAG)
        set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
        return()
    endif()

    set(CMAKE_REQUIRED_FLAGS "-march=x86-64-v3")

    # Test x86-64-v3 CPU support
    check_c_source_runs("
        #include <stdint.h>
        #ifdef _WIN32
        #include <intrin.h>
        #else
        #include <cpuid.h>
        #endif
        int main() {
            #ifdef _WIN32
            int info[4];
            __cpuid(info, 1);
            if (!(info[2] & (1 << 28))) return 1; // AVX
            __cpuidex(info, 7, 0);
            if (!(info[1] & (1 << 5))) return 1;  // AVX2
            if (!(info[1] & (1 << 3))) return 1;  // BMI1
            if (!(info[1] & (1 << 8))) return 1;  // BMI2
            __cpuid(info, 1);
            if (!(info[2] & (1 << 12))) return 1; // FMA
            if (!(info[2] & (1 << 29))) return 1; // F16C
            __cpuidex(info, 0x80000001, 0);
            if (!(info[2] & (1 << 5))) return 1;  // LZCNT
            __cpuid(info, 1);
            if (!(info[2] & (1 << 22))) return 1; // MOVBE
            if (!(info[2] & (1 << 27))) return 1; // OSXSAVE
            #else
            unsigned int eax, ebx, ecx, edx;
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_AVX)) return 1;
            if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ebx & bit_AVX2)) return 1;
            if (!(ebx & bit_BMI)) return 1;
            if (!(ebx & bit_BMI2)) return 1;
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_FMA)) return 1;
            if (!(ecx & bit_F16C)) return 1;
            if (!__get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_LZCNT)) return 1;
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_MOVBE)) return 1;
            if (!(ecx & bit_OSXSAVE)) return 1;
            #endif
            return 0;
        }
    " _CPU_SUPPORTS_V3)

    if(_CPU_SUPPORTS_V3)
        set(${DETECTED_LEVEL} "x86-64-v3" PARENT_SCOPE)
        set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
        return()
    endif()

    # Check x86-64-v2
    check_c_compiler_flag("-march=x86-64-v2" _COMPILER_SUPPORTS_V2_FLAG)
    if(NOT _COMPILER_SUPPORTS_V2_FLAG)
        set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
        return()
    endif()

    set(CMAKE_REQUIRED_FLAGS "-march=x86-64-v2")

    check_c_source_runs("
        #include <stdint.h>
        #ifdef _WIN32
        #include <intrin.h>
        #else
        #include <cpuid.h>
        #endif
        int main() {
            #ifdef _WIN32
            int info[4];
            __cpuid(info, 1);
            if (!(info[2] & (1 << 13))) return 1; // CMPXCHG16B
            __cpuidex(info, 0x80000001, 0);
            if (!(info[2] & (1 << 0))) return 1;  // LAHF-SAHF
            __cpuid(info, 1);
            if (!(info[2] & (1 << 23))) return 1; // POPCNT
            if (!(info[2] & (1 << 0))) return 1;  // SSE3
            if (!(info[2] & (1 << 19))) return 1; // SSE4.1
            if (!(info[2] & (1 << 20))) return 1; // SSE4.2
            if (!(info[2] & (1 << 9))) return 1;  // SSSE3
            #else
            unsigned int eax, ebx, ecx, edx;
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_CMPXCHG16B)) return 1;
            if (!__get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_LAHF)) return 1;
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_POPCNT)) return 1;
            if (!(ecx & bit_SSE3)) return 1;
            if (!(ecx & bit_SSE4_1)) return 1;
            if (!(ecx & bit_SSE4_2)) return 1;
            if (!(ecx & bit_SSSE3)) return 1;
            #endif
            return 0;
        }
    " _CPU_SUPPORTS_V2)

    set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})

    if(_CPU_SUPPORTS_V2)
        set(${DETECTED_LEVEL} "x86-64-v2" PARENT_SCOPE)
    endif()
endfunction()
