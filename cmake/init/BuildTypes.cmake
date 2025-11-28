# =============================================================================
# Build Types Configuration Module
# =============================================================================
# This module configures build types and sets default build type if not specified.
# Also handles build type specific flags and sanitizer configuration.
#
# Prerequisites:
#   - Early configuration must run before project() for musl decision
#   - Post-project configuration must run after project()
#   - Requires: Sanitizers.cmake, CompilerFlags.cmake (for configure_* functions)
#
# Functions:
#   - configure_build_type_early(): Sets default build type (before project())
#   - configure_build_type_post_project(): Configures build type specific flags (after project())
#
# Outputs:
#   - CMAKE_BUILD_TYPE set to default if not specified
#   - Build type specific flags and sanitizers configured
# =============================================================================

# =============================================================================
# Part 1: Early Configuration (before project())
# =============================================================================

function(configure_build_type_early)
    # Set default build type if not specified via command line (-DCMAKE_BUILD_TYPE=...)
    # This allows musl detection to work correctly based on build type
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug, Dev, Release, RelWithDebInfo, Coverage)" FORCE)
        message(STATUS "No build type specified, defaulting to Release")
    endif()
endfunction()

# =============================================================================
# Part 2: Post-project Configuration (after project())
# =============================================================================

function(configure_build_type_post_project)
    # Build type specific flags and sanitizer configuration
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Debug mode WITH sanitizers (default)
        # DEBUG_MEMORY works with ASan now (fixed recursion bug)
        configure_debug_memory(${USE_MIMALLOC} ${USE_MUSL} FALSE)
        configure_debug_build_flags("Debug")

        # Configure sanitizers (automatically handles mimalloc conflicts)
        # Note: Sanitizers are compatible with instrumentation - they work at different stages
        configure_sanitizers(${USE_MIMALLOC} "Debug")

        # Platform-specific sanitizer runtime fixes
        fix_macos_asan_runtime()
        copy_asan_runtime_dll()

    elseif(CMAKE_BUILD_TYPE STREQUAL "Dev")
        # Dev mode - debug WITHOUT sanitizers (faster iteration)
        # DEBUG_MEMORY disabled for performance (use Debug build for memory tracking)
        message(STATUS "DEBUG_MEMORY disabled in Dev mode for performance")
        configure_debug_build_flags("Dev")
        # No sanitizers in Dev mode

    elseif(CMAKE_BUILD_TYPE STREQUAL "Coverage")
        # Coverage build with instrumentation
        configure_coverage_flags()

    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        # Optimized release build (no debug symbols)
        configure_release_flags(${PLATFORM_DARWIN} ${PLATFORM_LINUX} ${IS_ROSETTA} ${IS_APPLE_SILICON} ${ASCIICHAT_ENABLE_CRC32_HW} FALSE)

    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        # RelWithDebInfo mode - optimized build with debug symbols
        # Like Release but with debug symbols enabled for debugging optimized code
        configure_release_flags(${PLATFORM_DARWIN} ${PLATFORM_LINUX} ${IS_ROSETTA} ${IS_APPLE_SILICON} ${ASCIICHAT_ENABLE_CRC32_HW} TRUE)

        # Enable errno backtraces for RelWithDebInfo (useful for debugging production issues)
        add_definitions(-DENABLE_ERRNO_BACKTRACES)
        message(STATUS "RelWithDebInfo: Enabled errno backtraces for debugging")

        # Windows-specific: Add debug info format for RelWithDebInfo
        if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
            add_compile_options(-gcodeview)
            add_link_options(-Wl,/DEBUG:FULL)
            message(STATUS "RelWithDebInfo: Enabled PDB generation with optimized code")
        endif()

    elseif(CMAKE_BUILD_TYPE STREQUAL "Sanitize")
        # Sanitize mode - focus on AddressSanitizer
        add_definitions(-DDEBUG_MEMORY)
        configure_debug_build_flags("Debug")
        configure_sanitizers(${USE_MIMALLOC} "Sanitize")
        copy_asan_runtime_dll()

    elseif(CMAKE_BUILD_TYPE STREQUAL "TSan")
        # Thread Sanitizer mode
        add_definitions(-DDEBUG_MEMORY -DDEBUG_THREADS)
        configure_sanitizers(${USE_MIMALLOC} "TSan")
    endif()
endfunction()

# Call early configuration automatically when this module is included
configure_build_type_early()

