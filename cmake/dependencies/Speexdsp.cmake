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

# For Windows: Try to find speexdsp via vcpkg first, then fall back to find_dependency_library
if(WIN32)
    # Try to find speexdsp from vcpkg directly
    find_library(SPEEXDSP_LIB NAMES speexdsp libspeexdsp
                 PATHS "${_VCPKG_INSTALLED_DIR}" NO_DEFAULT_PATH)
    find_path(SPEEXDSP_INC NAMES speex/speex_echo.h
              PATHS "${_VCPKG_INSTALLED_DIR}" NO_DEFAULT_PATH)

    if(SPEEXDSP_LIB AND SPEEXDSP_INC)
        set(SPEEXDSP_FOUND TRUE)
        set(SPEEXDSP_LIBRARIES "${SPEEXDSP_LIB}")
        set(SPEEXDSP_INCLUDE_DIRS "${SPEEXDSP_INC}")
        message(STATUS "${BoldGreen}SPEEXDSP${ColorReset} found (Windows Vcpkg): ${SPEEXDSP_LIB}")
    else()
        # Fall back to find_dependency_library for more robust searching
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
