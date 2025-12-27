# =============================================================================
# PortAudio Library Configuration
# =============================================================================
# Builds PortAudio from source with JACK explicitly disabled
#
# Platform-specific dependency management:
#   - Windows: Uses vcpkg (pre-built without JACK)
#   - Linux/macOS: Builds from source without JACK support
#   - Linux (musl): Built from source in MuslDependencies.cmake (without JACK)
#
# Why build from source?
#   System PortAudio packages (apt/dnf/brew) often include JACK support,
#   which pulls in Berkeley DB (~1.6MB) as a transitive dependency.
#   Building from source with --without-jack eliminates this.
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - ASCIICHAT_DEPS_CACHE_DIR: Dependency cache directory
#
# Outputs (variables set by this file):
#   - PORTAUDIO_LIBRARIES: Libraries to link against
#   - PORTAUDIO_INCLUDE_DIRS: Include directories
#   - PORTAUDIO_FOUND: Whether portaudio was found
#   - PORTAUDIO_BUILD_TARGET: CMake target for dependency tracking
# =============================================================================

# Windows uses vcpkg (already built without JACK in most vcpkg ports)
if(WIN32)
    include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)
    find_dependency_library(
        NAME PORTAUDIO
        VCPKG_NAMES portaudio
        HEADER portaudio.h
        PKG_CONFIG portaudio-2.0
        STATIC_LIB_NAME libportaudio.a
        REQUIRED
    )
# Linux/macOS: Build from source (unless musl, which is handled in MuslDependencies.cmake)
elseif(NOT USE_MUSL)
    include(${CMAKE_SOURCE_DIR}/cmake/dependencies/PortaudioFromSource.cmake)
endif()

# For musl builds, PortAudio is configured in MuslDependencies.cmake
# Variables PORTAUDIO_LIBRARIES, PORTAUDIO_INCLUDE_DIRS, PORTAUDIO_FOUND
# are already set there with --without-jack flag
