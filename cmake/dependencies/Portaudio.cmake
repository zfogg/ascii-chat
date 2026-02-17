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
    # vcpkg provides 'portaudio' target (not portaudio_static as of vcpkg 2024+)
    if(TARGET portaudio_static)
        set(PORTAUDIO_LIBRARIES portaudio_static)
        get_target_property(PORTAUDIO_INCLUDE_DIRS portaudio_static INTERFACE_INCLUDE_DIRECTORIES)
    elseif(TARGET portaudio)
        set(PORTAUDIO_LIBRARIES portaudio)
        get_target_property(PORTAUDIO_INCLUDE_DIRS portaudio INTERFACE_INCLUDE_DIRECTORIES)
    else()
        message(FATAL_ERROR "PortAudio found via vcpkg but no usable target (portaudio or portaudio_static)")
    endif()
    set(PORTAUDIO_FOUND TRUE)
    message(STATUS "Found PORTAUDIO via vcpkg: ${PORTAUDIO_LIBRARIES}")
else()
    # Use system PortAudio
    # On macOS Release builds: prefer static library when ASCIICHAT_SHARED_DEPS is OFF
    if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_SHARED_DEPS)
        find_library(PORTAUDIO_STATIC_LIB
            NAMES libportaudio.a
            PATHS
                ${HOMEBREW_PREFIX}/opt/portaudio/lib
            NO_DEFAULT_PATH
        )
        find_path(PORTAUDIO_INC NAMES portaudio.h
            PATHS
                ${HOMEBREW_PREFIX}/opt/portaudio/include
            NO_DEFAULT_PATH
        )

        if(PORTAUDIO_STATIC_LIB AND PORTAUDIO_INC)
            set(PORTAUDIO_LIBRARIES "${PORTAUDIO_STATIC_LIB}")
            set(PORTAUDIO_INCLUDE_DIRS "${PORTAUDIO_INC}")
            set(PORTAUDIO_FOUND TRUE)
            # PortAudio on macOS needs these frameworks
            list(APPEND PORTAUDIO_LIBRARIES
                "-framework CoreAudio"
                "-framework AudioToolbox"
                "-framework AudioUnit"
                "-framework CoreFoundation"
                "-framework CoreServices"
            )
            message(STATUS "Found ${BoldGreen}PortAudio${ColorReset} (macOS static): ${PORTAUDIO_STATIC_LIB}")
        endif()
    endif()

    # Fallback to pkg-config if static not found or not macOS Release
    if(NOT PORTAUDIO_FOUND)
        message(STATUS "PortAudio: Using system library via pkg-config")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
        set(PORTAUDIO_LIBRARIES ${PORTAUDIO_LINK_LIBRARIES})
        set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIRS})
        set(PORTAUDIO_FOUND TRUE)
        message(STATUS "Found PORTAUDIO via pkg-config: ${PORTAUDIO_LIBRARIES}")
    endif()
endif()
