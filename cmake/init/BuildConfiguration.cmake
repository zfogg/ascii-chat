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

# Option to build tests
option(BUILD_TESTS "Build test executables" ON)

# Build type (matches Makefile modes)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type: Debug, Dev, Release, Coverage" FORCE)
endif()

# Valid build types (matching Makefile)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Dev" "Release" "Coverage" "TSan")

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
    find_program(_asciichat_clang_tidy NAMES "${ASCIICHAT_CLANG_TIDY}" clang-tidy PATHS ENV PATH PATH_SUFFIXES bin NO_CACHE)
    if(NOT _asciichat_clang_tidy)
        message(WARNING "ASCIICHAT_ENABLE_ANALYZERS=ON but clang-tidy not found")
    else()
        set(CMAKE_C_CLANG_TIDY "${_asciichat_clang_tidy}" CACHE STRING "" FORCE)
    endif()

    find_program(_asciichat_cppcheck NAMES "${ASCIICHAT_CPPCHECK}" cppcheck PATHS ENV PATH PATH_SUFFIXES bin NO_CACHE)
    if(_asciichat_cppcheck)
        set(CMAKE_C_CPPCHECK "${_asciichat_cppcheck}" CACHE STRING "" FORCE)
    elseif(ASCIICHAT_CPPCHECK)
        message(WARNING "ASCIICHAT_ENABLE_ANALYZERS=ON but specified cppcheck binary '${ASCIICHAT_CPPCHECK}' not found")
    endif()

    if(NOT DEFINED ENV{ASCIICHAT_ANALYZER_SUPPRESS_WARNINGS})
        set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    endif()
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

