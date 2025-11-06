# =============================================================================
# PortAudio Library Configuration
# =============================================================================
# Finds and configures PortAudio library for audio capture/playback
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
#   - VCPKG_ROOT, VCPKG_TRIPLET: (Windows only) vcpkg configuration
#
# Outputs (variables set by this file):
#   - PORTAUDIO_LIBRARIES: Libraries to link against
#   - PORTAUDIO_INCLUDE_DIRS: Include directories
#   - PORTAUDIO_FOUND: Whether portaudio was found
# =============================================================================

if(WIN32)
    # Windows: Find PortAudio from vcpkg
    find_library(PORTAUDIO_LIBRARY_RELEASE NAMES portaudio PATHS "${VCPKG_LIB_PATH}" NO_DEFAULT_PATH)
    find_library(PORTAUDIO_LIBRARY_DEBUG NAMES portaudio PATHS "${VCPKG_DEBUG_LIB_PATH}" NO_DEFAULT_PATH)
    find_path(PORTAUDIO_INCLUDE_DIR NAMES portaudio.h PATHS "${VCPKG_INCLUDE_PATH}" NO_DEFAULT_PATH)

    if(PORTAUDIO_LIBRARY_RELEASE OR PORTAUDIO_LIBRARY_DEBUG)
        set(PORTAUDIO_LIBRARIES optimized ${PORTAUDIO_LIBRARY_RELEASE} debug ${PORTAUDIO_LIBRARY_DEBUG})
        set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIR})
        set(PORTAUDIO_FOUND TRUE)
        message(STATUS "Found ${BoldGreen}PortAudio${ColorReset}: ${PORTAUDIO_LIBRARY_RELEASE}")
    else()
        message(FATAL_ERROR "Could not find ${BoldRed}portaudio${ColorReset} - required dependency")
    endif()
else()
    # Unix/Linux/macOS: Use pkg-config
    # Skip pkg-config when using musl - dependencies are built from source
    if(NOT USE_MUSL)
        find_package(PkgConfig QUIET REQUIRED)
        pkg_check_modules(PORTAUDIO REQUIRED QUIET portaudio-2.0)
        if(PORTAUDIO_FOUND)
            message(STATUS "Checking for module '${BoldBlue}portaudio-2.0${ColorReset}'")
            message(STATUS "  Found ${BoldBlue}portaudio-2.0${ColorReset}, version ${BoldGreen}${PORTAUDIO_VERSION}${ColorReset}")
        endif()
    endif()
endif()
