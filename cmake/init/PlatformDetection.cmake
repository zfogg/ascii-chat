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
set(PLATFORM_IOS FALSE)

# Debug: message(STATUS "CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")

# Detect platform specifics like the Makefile
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PLATFORM_IOS TRUE)
    set(PLATFORM_POSIX TRUE)
    set(PLATFORM_DARWIN TRUE)
    message(STATUS "Platform: iOS (${CMAKE_OSX_ARCHITECTURES})")

elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
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
    if(IS_APPLE_SILICON AND NOT IS_ROSETTA)
        set(CMAKE_OSX_ARCHITECTURES arm64)
    endif()

    # Detect and set macOS SDK path for proper compilation
    # This is important for finding system headers with tools like the defer transformation tool
    # which use libclang/libTooling to process source files
    #
    # For Homebrew LLVM (self-contained, has its own headers), CMAKE_OSX_SYSROOT must be
    # cleared. CMake's project() auto-detects it via xcrun, which points to the Xcode SDK.
    # Homebrew LLVM uses the CommandLineTools SDK by default, so adding a second -isysroot
    # for the Xcode SDK causes header conflicts (missing size_t, ptrdiff_t, etc.).
    set(_is_homebrew_llvm FALSE)
    if(CMAKE_CXX_COMPILER MATCHES "homebrew" OR CMAKE_C_COMPILER MATCHES "homebrew" OR
       CMAKE_CXX_COMPILER MATCHES "/opt/homebrew" OR CMAKE_C_COMPILER MATCHES "/opt/homebrew" OR
       CMAKE_CXX_COMPILER MATCHES "/usr/local/opt/llvm" OR CMAKE_C_COMPILER MATCHES "/usr/local/opt/llvm")
        set(_is_homebrew_llvm TRUE)
    endif()

    if(_is_homebrew_llvm)
        # Homebrew LLVM is self-contained and finds system headers via its own driver.
        # CMAKE_OSX_SYSROOT (auto-set by CMake's project() from xcrun) adds a conflicting
        # -isysroot that breaks the include path ordering. Clear it.
        if(CMAKE_OSX_SYSROOT)
            message(STATUS "Using Homebrew LLVM - clearing CMAKE_OSX_SYSROOT (was: ${CMAKE_OSX_SYSROOT})")
            unset(CMAKE_OSX_SYSROOT CACHE)
            unset(CMAKE_OSX_SYSROOT)
        else()
            message(STATUS "Using Homebrew LLVM - skipping CMAKE_OSX_SYSROOT")
        endif()
    elseif(NOT CMAKE_OSX_SYSROOT)
        include(${CMAKE_SOURCE_DIR}/cmake/utils/DetectMacOSSDK.cmake)
        asciichat_detect_macos_sdk(_detected_sdk)
        if(_detected_sdk)
            set(CMAKE_OSX_SYSROOT "${_detected_sdk}" CACHE PATH "macOS SDK path" FORCE)
            message(STATUS "Detected macOS SDK: ${CMAKE_OSX_SYSROOT}")
        endif()
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

