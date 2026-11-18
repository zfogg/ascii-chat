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
#   - _asciichat_portaudio_link: Libraries to link against
#   - _asciichat_portaudio_include: Include directories
#   - _asciichat_portaudio_found: Whether portaudio was found
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
    if(NOT TARGET asciichat-portaudio)
        add_library(asciichat-portaudio INTERFACE)
    endif()

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
        asciichat_fetchcontent_declare(asciichat_portaudio
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/portaudio"
            EXCLUDE_FROM_ALL
            URL https://github.com/PortAudio/portaudio/archive/refs/tags/v19.7.0.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_GetProperties(asciichat_portaudio)
        if(NOT asciichat_portaudio_POPULATED)
            cmake_policy(PUSH)
            if(POLICY CMP0169)
                cmake_policy(SET CMP0169 OLD)
            endif()
            FetchContent_Populate(asciichat_portaudio)
            cmake_policy(POP)
        endif()
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
        set(ASCIICHAT_PORTAUDIO_TARGET "${_target}" PARENT_SCOPE)
        set(_asciichat_portaudio_link ${_target} PARENT_SCOPE)
        set(_asciichat_portaudio_include "${asciichat_portaudio_SOURCE_DIR}/include" PARENT_SCOPE)
    endfunction()
    asciichat_build_portaudio()
    if(NOT TARGET asciichat-portaudio)
        add_library(asciichat-portaudio INTERFACE)
        target_link_libraries(asciichat-portaudio INTERFACE "${ASCIICHAT_PORTAUDIO_TARGET}")
        target_include_directories(asciichat-portaudio INTERFACE
            "${asciichat_portaudio_SOURCE_DIR}/include")
    endif()
    return()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

# Windows must never fall through to Strawberry Perl's pkg-config. Prefer the
# staged vcpkg config target, then resolve the staged headers/library directly.
if(WIN32)
    # Use vcpkg's portaudio - our overlay port enables ALSA for device enumeration
    message(STATUS "PortAudio: Using staged Windows package")
    find_package(portaudio CONFIG QUIET)
    if(NOT TARGET portaudio AND NOT TARGET portaudio_static)
        find_path(_portaudio_include_dir NAMES portaudio.h
            PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
                  "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include"
            NO_DEFAULT_PATH)
        find_library(_portaudio_library NAMES portaudio portaudio_static
            PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
                  "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib"
            NO_DEFAULT_PATH)
        if(NOT _portaudio_include_dir OR NOT _portaudio_library)
            message(FATAL_ERROR "Windows PortAudio was not found in the staged vcpkg triplet; refusing pkg-config fallback")
        endif()
        add_library(portaudio-vcpkg UNKNOWN IMPORTED)
        set_target_properties(portaudio-vcpkg PROPERTIES
            IMPORTED_LOCATION "${_portaudio_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${_portaudio_include_dir}")
    endif()
    # vcpkg provides 'portaudio' target (not portaudio_static as of vcpkg 2024+)
    if(TARGET portaudio_static)
        set(_asciichat_portaudio_link portaudio_static)
        get_target_property(_asciichat_portaudio_include portaudio_static INTERFACE_INCLUDE_DIRECTORIES)
    elseif(TARGET portaudio)
        set(_asciichat_portaudio_link portaudio)
        get_target_property(_asciichat_portaudio_include portaudio INTERFACE_INCLUDE_DIRECTORIES)
    elseif(TARGET portaudio-vcpkg)
        set(_asciichat_portaudio_link portaudio-vcpkg)
        get_target_property(_asciichat_portaudio_include portaudio-vcpkg INTERFACE_INCLUDE_DIRECTORIES)
    else()
        message(FATAL_ERROR "PortAudio found via vcpkg but no usable target (portaudio or portaudio_static)")
    endif()
    set(_asciichat_portaudio_found TRUE)
    message(STATUS "Found PORTAUDIO via staged Windows package: ${_asciichat_portaudio_link}")
elseif(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    find_package(portaudio CONFIG QUIET)
    if(NOT TARGET portaudio AND NOT TARGET portaudio_static)
        message(FATAL_ERROR "PortAudio vcpkg config did not provide a usable target")
    endif()
    if(TARGET portaudio_static)
        set(_asciichat_portaudio_link portaudio_static)
    else()
        set(_asciichat_portaudio_link portaudio)
    endif()
    set(_asciichat_portaudio_found TRUE)
else()
    # Use system PortAudio
    # On macOS Release builds: prefer static library when BUILD_SHARED_LIBS is OFF
    if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT BUILD_SHARED_LIBS)
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
            set(_asciichat_portaudio_link "${PORTAUDIO_STATIC_LIB}")
            set(_asciichat_portaudio_include "${PORTAUDIO_INC}")
            set(_asciichat_portaudio_found TRUE)
            # PortAudio on macOS needs these frameworks
            list(APPEND _asciichat_portaudio_link
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
    if(NOT _asciichat_portaudio_found)
        message(STATUS "PortAudio: Using system library via pkg-config")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
        set(_asciichat_portaudio_link ${PORTAUDIO_LINK_LIBRARIES})
        set(_asciichat_portaudio_include ${_asciichat_portaudio_include})
        set(_asciichat_portaudio_found TRUE)
        message(STATUS "Found PORTAUDIO via pkg-config: ${_asciichat_portaudio_link}")
    endif()
endif()

# Stable target consumed by application code. Keep provider-specific variables
# local to this module; callers link the interface target.
if(NOT TARGET asciichat-portaudio)
    add_library(asciichat-portaudio INTERFACE)
    if(_asciichat_portaudio_link)
        target_link_libraries(asciichat-portaudio INTERFACE ${_asciichat_portaudio_link})
    endif()
    if(_asciichat_portaudio_include)
        target_include_directories(asciichat-portaudio INTERFACE ${_asciichat_portaudio_include})
    endif()
endif()
