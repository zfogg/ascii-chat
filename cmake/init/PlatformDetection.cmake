# =============================================================================
# Platform Detection Module
# =============================================================================
# This module detects the platform and sets platform-specific variables
#
# Prerequisites:
#   - Must run after project()
#   - CMAKE_SYSTEM_NAME must be set
#
# Outputs:
#   - PLATFORM_* variables (DARWIN, LINUX, WINDOWS, POSIX)
#   - IS_APPLE_SILICON and IS_ROSETTA for macOS
#   - PLATFORM_WINDOWS_* for Windows architecture
#   - _GNU_SOURCE defined for POSIX platforms
# =============================================================================

# Initialize all platform variables to FALSE
set(PLATFORM_DARWIN FALSE)
set(PLATFORM_LINUX FALSE)
set(PLATFORM_WINDOWS FALSE)
set(PLATFORM_POSIX FALSE)

# Detect platform specifics like the Makefile
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM_DARWIN TRUE)
    set(PLATFORM_POSIX TRUE)

    # Apple Silicon and Rosetta detection
    execute_process(
        COMMAND sysctl -n hw.optional.arm64
        OUTPUT_VARIABLE IS_APPLE_SILICON
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND sysctl -n sysctl.proc_translated
        OUTPUT_VARIABLE IS_ROSETTA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(NOT IS_APPLE_SILICON)
        set(IS_APPLE_SILICON 0)
    endif()
    if(NOT IS_ROSETTA)
        set(IS_ROSETTA 0)
    endif()

    # Force arm64 when building natively on Apple Silicon
    if(IS_APPLE_SILICON EQUAL 1 AND NOT IS_ROSETTA EQUAL 1)
        set(CMAKE_OSX_ARCHITECTURES arm64)
    endif()

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_LINUX TRUE)
    set(PLATFORM_POSIX TRUE)
    set(IS_APPLE_SILICON 0)
    set(IS_ROSETTA 0)

elseif(WIN32)
    add_definitions(-D_WIN32 -DWIN32_LEAN_AND_MEAN)
    set(PLATFORM_WINDOWS TRUE)
    set(IS_APPLE_SILICON 0)
    set(IS_ROSETTA 0)

    # Detect Windows architecture generically
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
        set(PLATFORM_WINDOWS_ARM64 TRUE)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM")
        set(PLATFORM_WINDOWS_ARM TRUE)
    else()
        set(PLATFORM_WINDOWS_X64 TRUE)
    endif()
else()
    set(PLATFORM_POSIX TRUE)
endif()

# Enable GNU extensions for POSIX functions (matches Makefile)
if(PLATFORM_POSIX AND NOT WIN32)
    add_definitions(-D_GNU_SOURCE)
endif()

