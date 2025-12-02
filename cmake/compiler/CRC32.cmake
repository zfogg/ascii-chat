# =============================================================================
# CRC32.cmake - Hardware CRC32 acceleration detection
# =============================================================================
# Detects CPU support for hardware-accelerated CRC32 instructions across
# different architectures (x86_64 SSE4.2, ARM CRC32).
#
# Sets:
#   ASCIICHAT_ENABLE_CRC32_HW - TRUE if hardware CRC32 is available
#
# Must be included AFTER platform detection and SIMD.cmake.
# =============================================================================

# Include centralized CPU detection
include(${CMAKE_SOURCE_DIR}/cmake/compiler/CPUDetection.cmake)

# CRC32 Hardware Acceleration
set(ASCIICHAT_CRC32_HW "auto" CACHE STRING "CRC32 hardware acceleration: auto, on, off")
set(ASCIICHAT_ENABLE_CRC32_HW FALSE)

if(ASCIICHAT_CRC32_HW STREQUAL "on")
    set(ASCIICHAT_ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
elseif(ASCIICHAT_CRC32_HW STREQUAL "auto")

    if(ASCIICHAT_IS_X86_64)
        # x86_64: Check for SSE4.2
        # If we already have SSSE3 or AVX2 from SIMD detection, assume SSE4.2 is available
        if(ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2)
            set(ASCIICHAT_ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
        else()
            # Try explicit SSE4.2 detection
            detect_x86_sse42()
            if(HAS_SSE42)
                set(ASCIICHAT_ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
            endif()
        endif()

    elseif(ASCIICHAT_IS_ARM64 OR ASCIICHAT_IS_ARM32)
        # ARM: Check for CRC32 extension
        detect_arm_crc32()
        if(HAS_ARM_CRC32)
            set(ASCIICHAT_ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
        endif()
    endif()
endif()

if(ASCIICHAT_ENABLE_CRC32_HW)
    add_definitions(-DHAVE_CRC32_HW)

    # Determine architecture for compiler flags
    if(ASCIICHAT_IS_ARM64 OR ASCIICHAT_IS_ARM32)
        set(CRC32_ARCH "ARM")
    elseif(ASCIICHAT_IS_X86_64)
        set(CRC32_ARCH "X86_64")
    endif()

    # Apply architecture-specific flags
    if(CRC32_ARCH STREQUAL "X86_64")
        add_compile_options(-msse4.2)
    elseif(CRC32_ARCH STREQUAL "ARM")
        # ARM CRC32 requires specific compiler flags
        if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
            add_compile_options(-march=armv8-a+crc)
        endif()
    endif()
endif()
