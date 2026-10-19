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

# =============================================================================
# iOS and WebAssembly builds use their platform audio APIs instead of PortAudio.
# =============================================================================
if(PLATFORM_IOS OR EMSCRIPTEN)
    if(PLATFORM_IOS)
        message(STATUS "Configuring ${BoldBlue}PortAudio${ColorReset} (iOS - skipped, using AVAudioEngine)...")
    else()
        message(STATUS "Configuring ${BoldBlue}PortAudio${ColorReset} (WebAssembly - skipped, using Web Audio)...")
    endif()

    # PortAudio is not suitable for iOS
    # iOS uses AVAudioEngine and the browser uses Web Audio for audio I/O.
    # Create a stub target for compatibility
    if(NOT TARGET portaudio)
        add_library(portaudio INTERFACE)
    endif()

    set(PORTAUDIO_FOUND FALSE)
    set(PORTAUDIO_LIBRARIES "")
    set(PORTAUDIO_INCLUDE_DIRS "")

    message(STATUS "${BoldYellow}⚠${ColorReset} PortAudio skipped for platform audio backend")
    return()
endif()


include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
if(USE_MUSL OR ASCIICHAT_BUILD_DEPS_FROM_SOURCE)
    function(asciichat_build_portaudio)
        asciichat_dependency_defaults()
        set(PA_BUILD_SHARED ${BUILD_SHARED_LIBS})
        set(PA_BUILD_STATIC ON)
        if(BUILD_SHARED_LIBS)
            set(PA_BUILD_STATIC OFF)
        endif()
        set(PA_BUILD_EXAMPLES OFF)
        set(PA_BUILD_TESTS OFF)
        set(PA_USE_JACK OFF)
        set(PA_USE_ASIO OFF)
        if(USE_MUSL)
            set(ALSA_INCLUDE_DIR "${ALSA_PREFIX}/include")
            set(ALSA_LIBRARY "${ALSA_PREFIX}/lib/libasound.a")
        endif()
        FetchContent_Declare(asciichat_portaudio
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_portaudio"
            EXCLUDE_FROM_ALL
            URL https://github.com/PortAudio/portaudio/archive/refs/tags/v19.7.0.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR asciichat-patch-first)
        FetchContent_MakeAvailable(asciichat_portaudio)
        set(PORTAUDIO_SOURCE_DIR "${asciichat_portaudio_SOURCE_DIR}")
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/patch-portaudio-cmake.cmake")
        add_subdirectory("${asciichat_portaudio_SOURCE_DIR}" "${asciichat_portaudio_BINARY_DIR}" EXCLUDE_FROM_ALL)
        if(BUILD_SHARED_LIBS)
            set(_target portaudio)
        else()
            set(_target portaudio_static)
        endif()
        if(TARGET alsa-lib-musl)
            add_dependencies(${_target} alsa-lib-musl)
        endif()
        set(PORTAUDIO_LIBRARIES ${_target} PARENT_SCOPE)
        set(PORTAUDIO_INCLUDE_DIRS "${asciichat_portaudio_SOURCE_DIR}/include" PARENT_SCOPE)
    endfunction()
    asciichat_build_portaudio()
    set(PORTAUDIO_FOUND TRUE)
    return()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

# Check if we're using vcpkg (CMAKE_TOOLCHAIN_FILE points to vcpkg)
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    # Use vcpkg's portaudio - our overlay port enables ALSA for device enumeration
    message(STATUS "PortAudio: Using vcpkg (overlay port with ALSA support)")
    # Try CMake config first, then fall back to pkg-config
    find_package(portaudio CONFIG QUIET)
    if(NOT portaudio_FOUND)
        include(FindPkgConfig)
        pkg_check_modules(portaudio REQUIRED portaudio-2.0)
        if(NOT TARGET portaudio)
            add_library(portaudio INTERFACE IMPORTED)
            target_include_directories(portaudio INTERFACE ${portaudio_INCLUDE_DIRS})
            target_link_libraries(portaudio INTERFACE ${portaudio_LIBRARIES})
        endif()
    endif()
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
