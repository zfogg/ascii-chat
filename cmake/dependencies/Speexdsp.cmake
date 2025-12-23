# =============================================================================
# Speex DSP Library Configuration
# =============================================================================
# Finds and configures Speex DSP library for acoustic echo cancellation
#
# Platform-specific dependency management:
#   - Windows: Uses vcpkg (CMake config or find_library fallback)
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Linux (musl): Dependencies built from source (see MuslDependencies.cmake)
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - VCPKG_ROOT, VCPKG_TARGET_TRIPLET: (Windows only) vcpkg configuration
#
# Outputs (variables set by this file):
#   - SPEEXDSP_LIBRARIES: Libraries to link against
#   - SPEEXDSP_INCLUDE_DIRS: Include directories
#   - SPEEXDSP_FOUND: Whether speexdsp was found
#   - HAVE_SPEEXDSP: Compile definition when found
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

if(WIN32 AND DEFINED ENV{VCPKG_ROOT})
    # Windows: Try find_package first (for CMake config files from vcpkg)
    find_package(speexdsp CONFIG QUIET)
    if(speexdsp_FOUND)
        set(SPEEXDSP_FOUND TRUE)
        set(SPEEXDSP_LIBRARIES speexdsp::speexdsp)
        message(STATUS "Found ${BoldGreen}SPEEXDSP${ColorReset} (vcpkg CMake config): speexdsp::speexdsp")
    else()
        # Fallback to find_library if no CMake config
        find_dependency_library(
            NAME SPEEXDSP
            VCPKG_NAMES speexdsp
            HEADER speex/speex_echo.h
            PKG_CONFIG speexdsp
            HOMEBREW_PKG speexdsp
            STATIC_LIB_NAME libspeexdsp.a
            REQUIRED
        )
    endif()
else()
    # Unix/Linux/macOS: Use find_dependency_library as normal
    find_dependency_library(
        NAME SPEEXDSP
        VCPKG_NAMES speexdsp
        HEADER speex/speex_echo.h
        PKG_CONFIG speexdsp
        HOMEBREW_PKG speexdsp
        STATIC_LIB_NAME libspeexdsp.a
        REQUIRED
    )
endif()

# Set compile definition for speexdsp
add_compile_definitions(HAVE_SPEEXDSP)
message(STATUS "Acoustic Echo Cancellation: ${BoldGreen}enabled${ColorReset}")
