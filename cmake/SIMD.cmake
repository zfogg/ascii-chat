# =============================================================================
# SIMD.cmake - SIMD instruction set detection and configuration
# =============================================================================
# Detects SIMD capabilities across different architectures (SSE2, SSSE3, AVX2,
# NEON, SVE) and configures appropriate compiler flags.
#
# Sets:
#   ENABLE_SIMD_SSE2, ENABLE_SIMD_SSSE3, ENABLE_SIMD_AVX2 - x86_64 SIMD support
#   ENABLE_SIMD_NEON, ENABLE_SIMD_SVE - ARM SIMD support
#
# Must be included AFTER platform detection.
# =============================================================================

# Hardware Acceleration Detection (cross-platform including Windows ARM)
# =============================================================================

include(CheckCSourceCompiles)
include(CheckCSourceRuns)

# User override controls
set(SIMD_MODE "auto" CACHE STRING "SIMD mode: auto, sse2, ssse3, avx2, neon, sve")
set_property(CACHE SIMD_MODE PROPERTY STRINGS "auto" "sse2" "ssse3" "avx2" "neon" "sve")

# Initialize SIMD flags
set(ENABLE_SIMD_SSE2 FALSE)
set(ENABLE_SIMD_SSSE3 FALSE)
set(ENABLE_SIMD_AVX2 FALSE)
set(ENABLE_SIMD_NEON FALSE)
set(ENABLE_SIMD_SVE FALSE)

# Check for user-specified SIMD mode
if(NOT SIMD_MODE STREQUAL "auto")
    # Manual mode - only enable the specific architecture requested
    if(SIMD_MODE STREQUAL "sse2")
        set(ENABLE_SIMD_SSE2 TRUE)
    elseif(SIMD_MODE STREQUAL "ssse3")
        set(ENABLE_SIMD_SSSE3 TRUE)
    elseif(SIMD_MODE STREQUAL "avx2")
        set(ENABLE_SIMD_AVX2 TRUE)
    elseif(SIMD_MODE STREQUAL "neon")
        set(ENABLE_SIMD_NEON TRUE)
    elseif(SIMD_MODE STREQUAL "sve")
        set(ENABLE_SIMD_SVE TRUE)
    endif()
else()
    # Auto-detect SIMD capabilities (cross-platform)

    # Windows-specific detection
    if(WIN32)
        # Check processor architecture on Windows
        # CMAKE_SYSTEM_PROCESSOR on Windows: AMD64, x86, ARM64, ARM
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
            # Windows on ARM64 - ARMv8 always has NEON
            set(ENABLE_SIMD_NEON TRUE)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM")
            # Windows on ARM32 - NEON is optional in ARMv7
            # Try compile test to check for NEON
            check_c_source_compiles("
                #include <arm_neon.h>
                int main() {
                    uint8x16_t a = vdupq_n_u8(0);
                    return 0;
                }
            " CAN_COMPILE_NEON)
            if(CAN_COMPILE_NEON)
                set(ENABLE_SIMD_NEON TRUE)
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
            # Windows x86_64 - try compile tests for SIMD features
            # Try to compile and run CPUID check for x86
            # Note: Clang on Windows may have issues with check_c_source_runs, so we use compile checks instead
            if(NOT CMAKE_CROSSCOMPILING AND NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))
                # Check from highest to lowest, stop when we find support
                # Higher instruction sets imply lower ones

                # Check for AVX2
                check_c_source_runs("
                    #include <intrin.h>
                    int main() {
                        int info[4];
                        __cpuidex(info, 7, 0);
                        if (info[1] & (1 << 5)) // AVX2
                            return 0;
                        return 1;
                    }
                " HAS_AVX2_RUNTIME)

                if(HAS_AVX2_RUNTIME)
                    set(ENABLE_SIMD_AVX2 TRUE)
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)
                else()
                    # Check for SSSE3
                    check_c_source_runs("
                        #include <intrin.h>
                        int main() {
                            int info[4];
                            __cpuid(info, 1);
                            if (info[2] & (1 << 9)) // SSSE3
                                return 0;
                            return 1;
                        }
                    " HAS_SSSE3_RUNTIME)

                    if(HAS_SSSE3_RUNTIME)
                        set(ENABLE_SIMD_SSSE3 TRUE)
                        set(ENABLE_SIMD_SSE2 TRUE)
                    else()
                        # Check for SSE2 (baseline for x86_64)
                        check_c_source_runs("
                            #include <intrin.h>
                            int main() {
                                int info[4];
                                __cpuid(info, 1);
                                if (info[3] & (1 << 26)) // SSE2
                                    return 0;
                                return 1;
                            }
                        " HAS_SSE2_RUNTIME)

                        if(HAS_SSE2_RUNTIME)
                            set(ENABLE_SIMD_SSE2 TRUE)
                        endif()
                    endif()
                endif()
            else()
                # Cross-compiling or Clang on Windows, use compile-time checks with appropriate flags
                # Save current flags
                set(CMAKE_REQUIRED_FLAGS_SAVE ${CMAKE_REQUIRED_FLAGS})

                # Test AVX2 with required flags
                set(CMAKE_REQUIRED_FLAGS "-mavx2")
                check_c_source_compiles("
                    #include <immintrin.h>
                    int main() { __m256i a = _mm256_setzero_si256(); return 0; }
                " CAN_COMPILE_AVX2)

                # Test SSSE3 with required flags
                set(CMAKE_REQUIRED_FLAGS "-mssse3")
                check_c_source_compiles("
                    #include <tmmintrin.h>
                    int main() { __m128i a = _mm_setzero_si128(); a = _mm_abs_epi8(a); return 0; }
                " CAN_COMPILE_SSSE3)

                # Test SSE2 with required flags
                set(CMAKE_REQUIRED_FLAGS "-msse2")
                check_c_source_compiles("
                    #include <emmintrin.h>
                    int main() { __m128i a = _mm_setzero_si128(); return 0; }
                " CAN_COMPILE_SSE2)

                # Restore flags
                set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})

                # Enable all instruction sets that can be compiled
                # Note: Higher SIMD levels include lower ones
                if(CAN_COMPILE_AVX2)
                    set(ENABLE_SIMD_AVX2 TRUE)
                    set(ENABLE_SIMD_SSSE3 TRUE)  # AVX2 includes SSSE3
                    set(ENABLE_SIMD_SSE2 TRUE)   # AVX2 includes SSE2
                elseif(CAN_COMPILE_SSSE3)
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)   # SSSE3 includes SSE2
                elseif(CAN_COMPILE_SSE2)
                    set(ENABLE_SIMD_SSE2 TRUE)
                endif()
            endif()
        endif()

    elseif(PLATFORM_DARWIN)
        # macOS detection (native)
        if(IS_APPLE_SILICON EQUAL 1)
            set(ENABLE_SIMD_NEON TRUE)
        elseif(IS_ROSETTA EQUAL 1)
            set(ENABLE_SIMD_SSSE3 TRUE)
            set(ENABLE_SIMD_SSE2 TRUE)  # SSSE3 includes SSE2
        else()
            # Intel Mac - check for AVX2 support
            execute_process(
                COMMAND sysctl -n hw.optional.avx2_0
                OUTPUT_VARIABLE HAS_AVX2_MAC
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(HAS_AVX2_MAC EQUAL 1)
                set(ENABLE_SIMD_AVX2 TRUE)
                set(ENABLE_SIMD_SSSE3 TRUE)  # AVX2 includes SSSE3
                set(ENABLE_SIMD_SSE2 TRUE)   # AVX2 includes SSE2
            else()
                set(ENABLE_SIMD_SSSE3 TRUE)
                set(ENABLE_SIMD_SSE2 TRUE)  # SSSE3 includes SSE2
            endif()
        endif()

    elseif(PLATFORM_LINUX)
        # Linux detection
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            # Linux ARM64 - ARMv8 always has NEON
            set(ENABLE_SIMD_NEON TRUE)
            # Check for SVE (optional extension)
            if(EXISTS "/proc/cpuinfo")
                file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
                if(CPUINFO_CONTENT MATCHES "sve")
                    # SVE is available in addition to NEON
                    set(ENABLE_SIMD_SVE TRUE)
                endif()
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
            # Linux ARM32 - NEON is optional
            if(EXISTS "/proc/cpuinfo")
                file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
                if(CPUINFO_CONTENT MATCHES "neon")
                    set(ENABLE_SIMD_NEON TRUE)
                endif()
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            # Linux x86_64 - use /proc/cpuinfo
            if(EXISTS "/proc/cpuinfo")
                file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
                # Check from highest to lowest, set all implied instruction sets
                if(CPUINFO_CONTENT MATCHES "avx2")
                    set(ENABLE_SIMD_AVX2 TRUE)
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)
                elseif(CPUINFO_CONTENT MATCHES "ssse3")
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)
                elseif(CPUINFO_CONTENT MATCHES "sse2")
                    set(ENABLE_SIMD_SSE2 TRUE)
                endif()
            endif()
        endif()
    endif()
endif()

# Apply SIMD compile definitions and flags
if(ENABLE_SIMD_SSE2 OR ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2 OR ENABLE_SIMD_NEON OR ENABLE_SIMD_SVE)
    add_definitions(-DSIMD_SUPPORT)

    # Prefer wider vector widths for SIMD-heavy workloads
    if(ENABLE_SIMD_AVX2)
        add_compile_options(-mprefer-vector-width=256)
    endif()
endif()

# Apply specific SIMD flags
if(ENABLE_SIMD_SSE2)
    add_definitions(-DSIMD_SUPPORT_SSE2)
    if(WIN32)
        add_compile_options(-msse2 -mno-mmx)  # Disable MMX on Windows
    else()
        add_compile_options(-msse2)
    endif()
endif()

if(ENABLE_SIMD_SSSE3)
    add_definitions(-DSIMD_SUPPORT_SSSE3)
    if(WIN32)
        add_compile_options(-mssse3 -mno-mmx)  # Disable MMX on Windows
    else()
        add_compile_options(-mssse3)
    endif()
endif()

if(ENABLE_SIMD_AVX2)
    add_definitions(-DSIMD_SUPPORT_AVX2)
    if(WIN32)
        add_compile_options(-mavx2 -mno-mmx)  # Disable MMX on Windows
    else()
        add_compile_options(-mavx2)
    endif()
endif()

if(ENABLE_SIMD_NEON)
    add_definitions(-DSIMD_SUPPORT_NEON)
    # Windows ARM64 with Clang needs proper arch flags
    if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|ARM64")
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            # Clang on Windows ARM might need explicit flags
            add_compile_options(-march=armv8-a+simd)
        endif()
    endif()
endif()

if(ENABLE_SIMD_SVE)
    add_definitions(-DSIMD_SUPPORT_SVE)
    add_compile_options(-march=armv8-a+sve)
endif()

# =============================================================================
