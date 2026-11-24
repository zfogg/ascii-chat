# =============================================================================
# x264 H.264/AVC Encoder Library Configuration
# =============================================================================
# Finds and configures x264 for H.264 encoding support in FFmpeg
#
# Platform-specific dependency management:
#   - musl: Built from source with the selected Clang musl target
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Windows: Uses vcpkg
#
# Output: `x264::x264` and `asciichat-x264`; `X264_PREFIX` is retained as
# install metadata for FFmpeg's autotools configure step.
# =============================================================================

# Stable snapshot from code.videolan.org (x264 doesn't do versioned releases)
set(X264_COMMIT "4613ac3c15fd75cebc4b9f65b7fb95e70a3acce1")
set(X264_HASH "44ce79258656d7dbe06165321cb989cc242a66effa308e25d2f0a197b50f8398")

# =============================================================================
# Windows: x264 not needed (vcpkg FFmpeg is built without libx264)
# =============================================================================
if(WIN32)
    message(STATUS "⚠ x264 not needed on Windows (vcpkg FFmpeg uses built-in codecs)")
    add_library(asciichat-x264 INTERFACE)
    return()
endif()

# Emscripten's web builds do not link the native FFmpeg encoder path.  Do not
# require a host x264 pkg-config module when generating the browser targets.
if(EMSCRIPTEN)
    message(STATUS "⚠ x264 not needed for Emscripten web targets")
    add_library(asciichat-x264 INTERFACE)
    return()
endif()

# =============================================================================
# musl: Build from source
# =============================================================================
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}x264${ColorReset} from source (musl)...")
    include(ExternalProject)

    set(X264_PREFIX "${MUSL_DEPS_DIR_STATIC}/x264")
    set(X264_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/x264-build")
    set(X264_SOURCE_DIR "${X264_BUILD_DIR}/src")

    if(NOT EXISTS "${X264_PREFIX}/lib/libx264.a")
        message(STATUS "  x264 library not found in cache, will build from source")

        file(MAKE_DIRECTORY "${X264_BUILD_DIR}")
        file(MAKE_DIRECTORY "${X264_SOURCE_DIR}")

        # Download x264 source
        set(X264_TARBALL "${X264_BUILD_DIR}/x264.tar.gz")
        if(EXISTS "${X264_TARBALL}")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar tzf "${X264_TARBALL}"
                RESULT_VARIABLE X264_ARCHIVE_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(NOT X264_ARCHIVE_RESULT EQUAL 0)
                message(STATUS "  Removing invalid cached x264 archive")
                file(REMOVE "${X264_TARBALL}")
            endif()
        endif()
        if(NOT EXISTS "${X264_TARBALL}")
            message(STATUS "  Downloading x264...")
            file(DOWNLOAD
                "https://codeload.github.com/mirror/x264/tar.gz/${X264_COMMIT}"
                "${X264_TARBALL}"
                EXPECTED_HASH SHA256=${X264_HASH}
                STATUS DOWNLOAD_STATUS
                SHOW_PROGRESS
            )
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                message(FATAL_ERROR "Failed to download x264: ${ERROR_MSG}")
            endif()
        endif()

        # Extract tarball
        if(NOT EXISTS "${X264_SOURCE_DIR}/configure")
            message(STATUS "  Extracting x264...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${X264_TARBALL}"
                WORKING_DIRECTORY "${X264_BUILD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract x264 tarball")
            endif()
            # Tarball extracts to x264-<commit>/
            file(GLOB X264_EXTRACTED_DIR "${X264_BUILD_DIR}/x264-${X264_COMMIT}*")
            if(X264_EXTRACTED_DIR)
                file(RENAME "${X264_EXTRACTED_DIR}" "${X264_SOURCE_DIR}")
            endif()
        endif()

        include(ProcessorCount)
        ProcessorCount(NPROC)
        if(NPROC EQUAL 0)
            set(NPROC 4)
        endif()
        string(REPLACE ";" " " _x264_standard_libraries "${CMAKE_C_STANDARD_LIBRARIES}")
        set(_x264_ldflags "${CMAKE_EXE_LINKER_FLAGS} ${_x264_standard_libraries}")
        set(_x264_configure_wrapper "${X264_BUILD_DIR}/configure-wrapper.sh")
        file(WRITE "${_x264_configure_wrapper}"
            "#!/bin/sh\n"
            "set -eu\n"
            "export CC='${ASCIICHAT_CLANG_EXECUTABLE}'\n"
            "export CFLAGS='${MUSL_KERNEL_CFLAGS} -fPIC'\n"
            "export LDFLAGS='${_x264_ldflags}'\n"
            "exec '${X264_SOURCE_DIR}/configure' \"$@\"\n")
        file(CHMOD "${_x264_configure_wrapper}"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)

        ExternalProject_Add(asciichat-x264-musl
            SOURCE_DIR "${X264_SOURCE_DIR}"
            CONFIGURE_COMMAND "${_x264_configure_wrapper}"
                --prefix=${X264_PREFIX}
                --enable-static
                --disable-shared
                --enable-pic
                --disable-cli
                --disable-asm
                --disable-opencl
                --disable-avs
                --disable-swscale
                --disable-lavf
                --disable-ffms
                --disable-gpac
                --disable-lsmash
            BUILD_COMMAND ${CMAKE_COMMAND} -E env
                "CC=${ASCIICHAT_CLANG_EXECUTABLE}" make -j${NPROC}
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS "${X264_PREFIX}/lib/libx264.a"
            LOG_CONFIGURE TRUE LOG_BUILD TRUE LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE)
        unset(_x264_ldflags)
        unset(_x264_standard_libraries)
        unset(_x264_configure_wrapper)
        message(STATUS "  ${BoldGreen}x264${ColorReset} build scheduled through ExternalProject")
    else()
        message(STATUS "  ${BoldGreen}x264${ColorReset} library found in cache: ${BoldMagenta}${X264_PREFIX}/lib/libx264.a${ColorReset}")
    endif()

    file(MAKE_DIRECTORY "${X264_PREFIX}/include" "${X264_PREFIX}/lib")

    if(NOT TARGET x264::x264)
        add_library(x264::x264 STATIC IMPORTED GLOBAL)
        set_target_properties(x264::x264 PROPERTIES
            IMPORTED_LOCATION "${X264_PREFIX}/lib/libx264.a"
            INTERFACE_INCLUDE_DIRECTORIES "${X264_PREFIX}/include"
        )
    endif()
    if(TARGET asciichat-x264-musl)
        add_dependencies(x264::x264 asciichat-x264-musl)
    endif()

    if(NOT TARGET asciichat-x264)
        add_library(asciichat-x264 INTERFACE)
        target_link_libraries(asciichat-x264 INTERFACE x264::x264)
    endif()
    message(STATUS "${BoldGreen}✓${ColorReset} x264 (musl): ${X264_PREFIX}/lib/libx264.a")
    return()
endif()

# =============================================================================
# Non-musl: Use system package via pkg-config
# =============================================================================
include(FindPkgConfig)
pkg_check_modules(_asciichat_x264 REQUIRED x264)

if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT BUILD_SHARED_LIBS)
    find_library(_asciichat_x264_static_library
        NAMES libx264.a
        PATHS ${_asciichat_x264_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )
    if(NOT _asciichat_x264_static_library)
        message(FATAL_ERROR "Static x264 is required for portable macOS Release builds")
    endif()
    set(_asciichat_x264_link "${_asciichat_x264_static_library}")
    message(STATUS "${BoldGreen}✓${ColorReset} x264 (static): ${_asciichat_x264_static_library}")
else()
    set(_asciichat_x264_link "${_asciichat_x264_LDFLAGS}")
    message(STATUS "${BoldGreen}✓${ColorReset} x264 found")
endif()

if(NOT TARGET x264::x264)
    add_library(x264::x264 INTERFACE IMPORTED)
    target_include_directories(x264::x264 SYSTEM INTERFACE ${_asciichat_x264_INCLUDE_DIRS})
    target_link_libraries(x264::x264 INTERFACE ${_asciichat_x264_link})
endif()
if(NOT TARGET asciichat-x264)
    add_library(asciichat-x264 INTERFACE)
    target_link_libraries(asciichat-x264 INTERFACE x264::x264)
endif()
