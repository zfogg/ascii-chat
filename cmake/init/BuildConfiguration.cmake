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

# C standard selection - intelligently detect the best available standard
# Uses modern CMake compiler feature detection instead of manual flag checking
# Only set C23 for GNU/Clang compilers
if(NOT CMAKE_C_STANDARD)
    # Only configure C23 for Clang and GCC compilers
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
        # Set C23 standard - CMake will automatically handle fallback to C2X if needed
        # CMAKE_C_STANDARD_REQUIRED OFF allows CMake to gracefully fall back
        set(CMAKE_C_STANDARD 23)
        set(CMAKE_C_STANDARD_REQUIRED OFF)  # Allow fallback to C2X if C23 not available

        # Try strict C23 without GNU extensions
        set(CMAKE_C_EXTENSIONS OFF)

        # Verify the standard was set (CMake will warn if it falls back)
        message(STATUS "Using ${BoldCyan}C23${ColorReset} standard (will fall back to ${Yellow}C2X${ColorReset} if not supported)")
    else()
        # Non-Clang/GCC compilers: use C17 (widely supported)
        set(CMAKE_C_STANDARD 17)
        set(CMAKE_C_STANDARD_REQUIRED ON)
        set(CMAKE_C_EXTENSIONS ON)
        message(STATUS "Using C17 standard for ${CMAKE_C_COMPILER_ID} compiler")
    endif()
endif()

# Note: CMAKE_C_STANDARD_REQUIRED is set per-compiler above
# This ensures we get the best available standard with graceful fallback

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
# Automatically disable in Docker when USE_MUSL is OFF (no static linking available)
if(EXISTS "/.dockerenv" AND NOT USE_MUSL AND UNIX AND NOT APPLE)
    set(_default_enforce_static OFF)
    message(STATUS "Docker detected without USE_MUSL - static linking enforcement ${BoldYellow}auto-disabled${ColorReset}")
else()
    set(_default_enforce_static ON)
endif()
option(ASCIICHAT_ENFORCE_STATIC_RELEASE "Fail Release builds if binaries are not statically linked" ${_default_enforce_static})

# Skip hardening validation (useful for CI performance tests where linker checks may fail)
option(ASCIICHAT_SKIP_HARDENING_VALIDATION "Skip security hardening validation for Release binaries" OFF)

# Build type (matches Makefile modes)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type: Debug, Dev, Release, RelWithDebInfo" FORCE)
endif()

# Valid build types (matching Makefile)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Dev" "Release" "RelWithDebInfo" "TSan")

# Interprocedural optimization (LTO) support detection
include(CheckIPOSupported)
set(ASCIICHAT_ENABLE_IPO FALSE CACHE INTERNAL "Enable IPO for release builds")
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
    if(IPO_SUPPORTED)
        set(ASCIICHAT_ENABLE_IPO TRUE CACHE INTERNAL "Enable IPO for release builds" FORCE)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
        message(STATUS "Interprocedural optimization ${BoldGreen}enabled${ColorReset} for ${CMAKE_BUILD_TYPE} builds")
    else()
        message(STATUS "Interprocedural optimization ${BoldYellow}disabled${ColorReset}: ${IPO_ERROR}")
    endif()
endif()

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

