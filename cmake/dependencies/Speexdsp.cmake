# =============================================================================
# Speex DSP Library Configuration
# =============================================================================
# Finds and configures Speex DSP library for acoustic echo cancellation
#
# Platform-specific dependency management:
#   - Windows: Uses vcpkg
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

find_dependency_library(
    NAME SPEEXDSP
    VCPKG_NAMES speexdsp
    HEADER speex/speex_echo.h
    PKG_CONFIG speexdsp
    HOMEBREW_PKG speexdsp
    STATIC_LIB_NAME libspeexdsp.a
    REQUIRED
)

# Set compile definition for speexdsp
add_compile_definitions(HAVE_SPEEXDSP)
message(STATUS "Acoustic Echo Cancellation: ${BoldGreen}enabled${ColorReset}")
