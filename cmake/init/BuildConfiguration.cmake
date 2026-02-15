# =============================================================================
# Build Configuration Module
# =============================================================================
# This module configures build settings: terminal colors, CPU cores, C standard,
# build types, and output directories
#
# Prerequisites:
#   - Must run after project()
#   - CMAKE_SYSTEM_NAME must be set
#
# Outputs:
#   - Color* variables for terminal output
#   - CPU_CORES for parallel builds
#   - CMAKE_C_STANDARD set to 23
#   - CMAKE_*_OUTPUT_DIRECTORY set
# =============================================================================

# Language standards (Clang only)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(STATUS "Using ${BoldCyan}C23${ColorReset} and ${BoldCyan}C++26${ColorReset} standards")

# =============================================================================
# Windows Install Prefix Configuration
# =============================================================================
# Set CMAKE_INSTALL_PREFIX to match WiX installer default location
# WiX installs to "Program Files\ascii-chat" (64-bit), so we match that
# This ensures `cmake --install build` and the MSI installer use the same location
#
# Architecture considerations:
# - 64-bit Windows: "C:/Program Files/ascii-chat" (native 64-bit apps)
# - 32-bit Windows: "C:/Program Files/ascii-chat" (only Program Files exists)
# - 32-bit app on 64-bit Windows: CMake defaults to "Program Files (x86)" which is correct
#
# We only override for 64-bit builds to use the 64-bit Program Files directory
if(WIN32 AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit build: use 64-bit Program Files
        set(CMAKE_INSTALL_PREFIX "C:/Program Files/ascii-chat" CACHE PATH "Installation directory" FORCE)
        message(STATUS "Install prefix: ${BoldCyan}C:/Program Files/ascii-chat${ColorReset} (64-bit)")
    else()
        # 32-bit build: CMake's default Program Files (x86) is correct on 64-bit Windows
        # On 32-bit Windows, there's only Program Files, which CMake also handles correctly
        message(STATUS "Install prefix: ${BoldCyan}${CMAKE_INSTALL_PREFIX}${ColorReset} (32-bit)")
    endif()
endif()

# Option to build tests
option(BUILD_TESTS "Build test executables" ON)

# Enforce static linking for Release builds by default
# Automatically disable when USE_MUSL is OFF on Linux (no static linking available)
# - Docker without musl: dynamic glibc linking
# - ARM64 Linux: musl not enabled by default (limited GitHub runner support)
if(WIN32)
    # Windows uses shared libraries (DLLs), not static linking
    set(_default_enforce_static OFF)
elseif(NOT USE_MUSL AND UNIX AND NOT APPLE)
    # Check if we're on ARM64 (CMAKE_SYSTEM_PROCESSOR may not be set yet, use uname)
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE _host_arch
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(EXISTS "/.dockerenv")
        set(_default_enforce_static OFF)
        message(STATUS "Docker detected without USE_MUSL - static linking enforcement ${BoldYellow}auto-disabled${ColorReset}")
    elseif(_host_arch STREQUAL "aarch64" OR _host_arch STREQUAL "arm64")
        set(_default_enforce_static OFF)
        message(STATUS "ARM64 Linux without USE_MUSL - static linking enforcement ${BoldYellow}auto-disabled${ColorReset}")
    else()
        set(_default_enforce_static ON)
    endif()
else()
    set(_default_enforce_static ON)
endif()
option(ASCIICHAT_ENFORCE_STATIC_RELEASE "Fail Release builds if binaries are not statically linked" ${_default_enforce_static})

# Skip hardening validation
option(ASCIICHAT_SKIP_HARDENING_VALIDATION "Skip security hardening validation for Release binaries" OFF)

# Build type (matches Makefile modes)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type: Debug, Dev, Release, RelWithDebInfo" FORCE)
endif()

# Valid build types (matching Makefile)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Dev" "Release" "RelWithDebInfo" "TSan")

# NOTE: Interprocedural optimization (LTO) check has been moved to CMakeLists.txt
# AFTER project() is called, since check_ipo_supported() requires languages to be enabled.
# See CMakeLists.txt line ~105 for post-project() config section.
# Initialize here so it's available throughout the build
set(ASCIICHAT_ENABLE_IPO FALSE CACHE INTERNAL "Enable IPO for release builds")

if(USE_MUSL)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    list(APPEND _ascii_chat_try_compile_vars USE_MUSL)
endif()

list(APPEND _ascii_chat_try_compile_vars
    ASCIICHAT_RELEASE_CPU_TUNE
    ASCIICHAT_RELEASE_ENABLE_FAST_MATH
    ASCIICHAT_RELEASE_KEEP_FRAME_POINTERS
    ASCIICHAT_ENABLE_UNITY_BUILDS
)

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES "${_ascii_chat_try_compile_vars}")

if(ASCIICHAT_ENABLE_ANALYZERS)
    # Determine which clang-tidy to use
    # Priority: 1. User override (ASCIICHAT_CLANG_TIDY), 2. Centralized from FindPrograms.cmake
    set(_clang_tidy_for_analyzer "")
    if(ASCIICHAT_CLANG_TIDY)
        # User explicitly specified clang-tidy path
        set(_clang_tidy_for_analyzer "${ASCIICHAT_CLANG_TIDY}")
    else()
        # Use centralized ASCIICHAT_CLANG_TIDY_EXECUTABLE from FindPrograms.cmake
        set(_clang_tidy_for_analyzer "${ASCIICHAT_CLANG_TIDY_EXECUTABLE}")
    endif()
    if(NOT _clang_tidy_for_analyzer)
        message(WARNING "ASCIICHAT_ENABLE_ANALYZERS=ON but clang-tidy not found")
    else()
        set(CMAKE_C_CLANG_TIDY "${_clang_tidy_for_analyzer}" CACHE STRING "" FORCE)
        message(STATUS "Static analyzer: ${BoldCyan}clang-tidy${ColorReset} (${_clang_tidy_for_analyzer}) enabled")
    endif()

    # Determine which cppcheck to use
    # Priority: 1. User override (ASCIICHAT_CPPCHECK), 2. Auto-detected (ASCIICHAT_CPPCHECK_EXECUTABLE)
    set(_cppcheck_for_analyzer "")
    if(ASCIICHAT_CPPCHECK)
        set(_cppcheck_for_analyzer "${ASCIICHAT_CPPCHECK}")
    elseif(ASCIICHAT_CPPCHECK_EXECUTABLE)
        set(_cppcheck_for_analyzer "${ASCIICHAT_CPPCHECK_EXECUTABLE}")
    endif()
    if(_cppcheck_for_analyzer)
        # Use quiet mode to suppress verbose "Checking..." messages and informational notes
        set(CMAKE_C_CPPCHECK
            "${_cppcheck_for_analyzer}"
            "--quiet"
            "--suppress=normalCheckLevelMaxBranches"
            CACHE STRING "" FORCE)
    endif()

    if(NOT DEFINED ENV{ASCIICHAT_ANALYZER_SUPPRESS_WARNINGS})
        set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    endif()
else()
    # Clear stale analyzer settings when analyzers are disabled
    set(CMAKE_C_CLANG_TIDY "" CACHE STRING "" FORCE)
    set(CMAKE_C_CPPCHECK "" CACHE STRING "" FORCE)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

