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
#   - VCPKG_ROOT, VCPKG_TARGET_TRIPLET: (Windows only) vcpkg configuration
#
# Outputs (variables set by this file):
#   - PORTAUDIO_LIBRARIES: Libraries to link against
#   - PORTAUDIO_INCLUDE_DIRS: Include directories
#   - PORTAUDIO_FOUND: Whether portaudio was found
# =============================================================================

# For musl builds, PortAudio is configured in MuslDependencies.cmake
# with musl-gcc compiler. Skip this file entirely for musl.
if(USE_MUSL)
    return()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

# TEMPORARY FIX: vcpkg's PortAudio doesn't detect audio devices
# Force use of system PortAudio via pkg-config instead
message(STATUS "PortAudio: Preferring system library (vcpkg build has device detection issues)")

find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

set(PORTAUDIO_LIBRARIES ${PORTAUDIO_LINK_LIBRARIES})
set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIRS})
set(PORTAUDIO_FOUND TRUE)

message(STATUS "Found PORTAUDIO via pkg-config: ${PORTAUDIO_LIBRARIES}")
