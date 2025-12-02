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

# Include centralized CPU detection
include(${CMAKE_SOURCE_DIR}/cmake/compiler/CPUDetection.cmake)

# User override controls
set(ASCIICHAT_SIMD_MODE "auto" CACHE STRING "SIMD mode: auto, sse2, ssse3, avx2, neon, sve")
set_property(CACHE ASCIICHAT_SIMD_MODE PROPERTY STRINGS "auto" "sse2" "ssse3" "avx2" "neon" "sve")

# Initialize SIMD flags
set(ENABLE_SIMD_SSE2 FALSE)
set(ENABLE_SIMD_SSSE3 FALSE)
set(ENABLE_SIMD_AVX2 FALSE)
set(ENABLE_SIMD_NEON FALSE)
set(ENABLE_SIMD_SVE FALSE)

# Check for user-specified SIMD mode
if(NOT ASCIICHAT_SIMD_MODE STREQUAL "auto")
    # Manual mode - only enable the specific architecture requested
    if(ASCIICHAT_SIMD_MODE STREQUAL "sse2")
        set(ENABLE_SIMD_SSE2 TRUE)
    elseif(ASCIICHAT_SIMD_MODE STREQUAL "ssse3")
        set(ENABLE_SIMD_SSSE3 TRUE)
    elseif(ASCIICHAT_SIMD_MODE STREQUAL "avx2")
        set(ENABLE_SIMD_AVX2 TRUE)
    elseif(ASCIICHAT_SIMD_MODE STREQUAL "neon")
        set(ENABLE_SIMD_NEON TRUE)
    elseif(ASCIICHAT_SIMD_MODE STREQUAL "sve")
        set(ENABLE_SIMD_SVE TRUE)
    endif()
else()
    # Auto-detect SIMD capabilities using CPUDetection functions

    if(ASCIICHAT_IS_X86_64)
        # Detect x86_64 SIMD features
        detect_x86_simd_features()

        # Enable only the highest available (higher includes lower)
        if(HAS_AVX2)
            set(ENABLE_SIMD_AVX2 TRUE)
        elseif(HAS_SSSE3)
            set(ENABLE_SIMD_SSSE3 TRUE)
        elseif(HAS_SSE2)
            set(ENABLE_SIMD_SSE2 TRUE)
        endif()

    elseif(ASCIICHAT_IS_ARM64 OR ASCIICHAT_IS_ARM32)
        # Detect ARM SIMD features
        detect_arm_neon()
        if(HAS_NEON)
            set(ENABLE_SIMD_NEON TRUE)
        endif()

        # Check for SVE (ARM64 only)
        if(ASCIICHAT_IS_ARM64)
            detect_arm_sve()
            if(HAS_SVE)
                set(ENABLE_SIMD_SVE TRUE)
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
    add_definitions(-DSIMD_SUPPORT_SSE2=1)
    if(WIN32)
        add_compile_options(-msse2 -mno-mmx)  # Disable MMX on Windows
    else()
        add_compile_options(-msse2)
    endif()
else()
    add_definitions(-DSIMD_SUPPORT_SSE2=0)
endif()

if(ENABLE_SIMD_SSSE3)
    add_definitions(-DSIMD_SUPPORT_SSSE3=1)
    if(WIN32)
        add_compile_options(-mssse3 -mno-mmx)  # Disable MMX on Windows
    else()
        add_compile_options(-mssse3)
    endif()
else()
    add_definitions(-DSIMD_SUPPORT_SSSE3=0)
endif()

if(ENABLE_SIMD_AVX2)
    add_definitions(-DSIMD_SUPPORT_AVX2=1)
    if(WIN32)
        add_compile_options(-mavx2 -mno-mmx)  # Disable MMX on Windows
    else()
        add_compile_options(-mavx2)
    endif()
else()
    add_definitions(-DSIMD_SUPPORT_AVX2=0)
endif()

if(ENABLE_SIMD_NEON)
    add_definitions(-DSIMD_SUPPORT_NEON=1)
    # Windows ARM64 with Clang needs proper arch flags
    if(WIN32 AND ASCIICHAT_IS_ARM64)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            add_compile_options(-march=armv8-a+simd)
        endif()
    endif()
else()
    add_definitions(-DSIMD_SUPPORT_NEON=0)
endif()

if(ENABLE_SIMD_SVE)
    add_definitions(-DSIMD_SUPPORT_SVE=1)
    add_compile_options(-march=armv8-a+sve)
else()
    add_definitions(-DSIMD_SUPPORT_SVE=0)
endif()

# =============================================================================
