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

# Check if we're using vcpkg (CMAKE_TOOLCHAIN_FILE points to vcpkg)
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    # Use vcpkg's portaudio - our overlay port enables ALSA for device enumeration
    message(STATUS "PortAudio: Using vcpkg (overlay port with ALSA support)")
    find_package(portaudio CONFIG REQUIRED)
    # vcpkg provides portaudio_static target for static builds
    set(PORTAUDIO_LIBRARIES portaudio_static)
    get_target_property(PORTAUDIO_INCLUDE_DIRS portaudio_static INTERFACE_INCLUDE_DIRECTORIES)
    set(PORTAUDIO_FOUND TRUE)
    message(STATUS "Found PORTAUDIO via vcpkg: ${PORTAUDIO_LIBRARIES}")
else()
    # Use system PortAudio via pkg-config
    message(STATUS "PortAudio: Using system library via pkg-config")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    set(PORTAUDIO_LIBRARIES ${PORTAUDIO_LINK_LIBRARIES})
    set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIRS})
    set(PORTAUDIO_FOUND TRUE)
    message(STATUS "Found PORTAUDIO via pkg-config: ${PORTAUDIO_LIBRARIES}")
endif()
