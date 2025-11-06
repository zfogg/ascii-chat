# =============================================================================
# CRC32.cmake - Hardware CRC32 acceleration detection
# =============================================================================
# Detects CPU support for hardware-accelerated CRC32 instructions across
# different architectures (x86_64 SSE4.2, ARM CRC32).
#
# Sets:
#   ENABLE_CRC32_HW - TRUE if hardware CRC32 is available
#
# Must be included AFTER platform detection.
# =============================================================================

# CRC32 Hardware Acceleration
set(CRC32_HW "auto" CACHE STRING "CRC32 hardware acceleration: auto, on, off")
set(ENABLE_CRC32_HW FALSE)

if(CRC32_HW STREQUAL "on")
    set(ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
elseif(CRC32_HW STREQUAL "auto")
    if(WIN32)
        # Windows CRC32 detection
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|ARM64")
            # Try to detect CRC32 support on ARM
            # ARMv8-A includes optional CRC32, but not all implementations have it
            if(NOT CMAKE_CROSSCOMPILING)
                # Try runtime detection for ARM CRC32
                check_c_source_runs("
                    #include <arm_acle.h>
                    #include <stdint.h>
                    int main() {
                        uint32_t crc = 0;
                        uint8_t data = 0x42;
                        crc = __crc32b(crc, data);
                        return 0;
                    }
                " HAS_ARM_CRC32_RUNTIME)
                if(HAS_ARM_CRC32_RUNTIME)
                    set(ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
                endif()
            else()
                # Cross-compiling - try compile test with required flags
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
                " CAN_COMPILE_ARM_CRC32)
                unset(CMAKE_REQUIRED_FLAGS)
                if(CAN_COMPILE_ARM_CRC32)
                    set(ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
                endif()
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
            # Check for SSE4.2 support on x86_64
            # Note: Clang on Windows has issues with check_c_source_runs, so check SIMD flags first
            if(ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2)
                # Assume SSE4.2 if we have SSSE3 or better
                set(ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
            elseif(NOT CMAKE_CROSSCOMPILING AND NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))
                check_c_source_runs("
                    #include <intrin.h>
                    int main() {
                        int info[4];
                        __cpuid(info, 1);
                        if (info[2] & (1 << 20)) // SSE4.2
                            return 0;
                        return 1;
                    }
                " HAS_SSE42_RUNTIME)
                if(HAS_SSE42_RUNTIME)
                    set(ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
                endif()
            endif()
        endif()
    elseif(PLATFORM_DARWIN)
        # macOS detection
        if(IS_APPLE_SILICON EQUAL 1)
            # Apple Silicon M1/M2/M3 all have CRC32
            set(ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
        else()
            # Intel Mac - check for SSE4.2
            execute_process(
                COMMAND sysctl -n hw.optional.sse4_2
                OUTPUT_VARIABLE HAS_SSE42_MAC
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(HAS_SSE42_MAC EQUAL 1)
                set(ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
            endif()
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM|ARM64")
        # Linux/Generic ARM - try to detect CRC32 support
        if(PLATFORM_LINUX AND EXISTS "/proc/cpuinfo")
            file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
            if(CPUINFO_CONTENT MATCHES "crc32")
                set(ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
            endif()
        else()
            # Try compile test as fallback with required flags
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
            " CAN_COMPILE_ARM_CRC32)
            unset(CMAKE_REQUIRED_FLAGS)
            if(CAN_COMPILE_ARM_CRC32)
                set(ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
            endif()
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        # Linux x86_64 - assume SSE4.2 if we have SSSE3
        if(ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2)
            set(ENABLE_CRC32_HW TRUE CACHE INTERNAL "CRC32 HW support" FORCE)
        endif()
    endif()
endif()

if(ENABLE_CRC32_HW)
    add_definitions(-DHAVE_CRC32_HW)

    # Determine architecture more reliably
    if(PLATFORM_DARWIN AND IS_APPLE_SILICON EQUAL 1)
        # Apple Silicon - use ARM flags
        set(CRC32_ARCH "ARM")
    elseif(PLATFORM_DARWIN)
        # Intel Mac - use x86_64 flags
        set(CRC32_ARCH "X86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|ARM64|aarch64|arm64")
        set(CRC32_ARCH "ARM")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
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
