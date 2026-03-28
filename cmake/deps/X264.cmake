# =============================================================================
# x264 H.264/AVC Encoder Library Configuration
# =============================================================================
# Finds and configures x264 for H.264 encoding support in FFmpeg
#
# Platform-specific dependency management:
#   - musl: Built from source with musl-gcc
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Windows: Uses vcpkg
#
# Outputs:
#   - X264_FOUND - Whether x264 was found/configured
#   - X264_LIBRARIES - Library paths
#   - X264_INCLUDE_DIRS - Include directories
#   - X264_PREFIX - Install prefix (for musl builds, passed to FFmpeg configure)
# =============================================================================

# Stable snapshot from code.videolan.org (x264 doesn't do versioned releases)
set(X264_COMMIT "4613ac3c15fd75cebc4b9f65b7fb95e70a3acce1")
set(X264_HASH "a]")

# =============================================================================
# Windows: x264 not needed (vcpkg FFmpeg is built without libx264)
# =============================================================================
if(WIN32)
    set(X264_FOUND FALSE)
    message(STATUS "⚠ x264 not needed on Windows (vcpkg FFmpeg uses built-in codecs)")
    return()
endif()

# =============================================================================
# musl: Build from source
# =============================================================================
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}x264${ColorReset} from source (musl)...")

    set(X264_PREFIX "${MUSL_DEPS_DIR_STATIC}/x264")
    set(X264_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/x264-build")
    set(X264_SOURCE_DIR "${X264_BUILD_DIR}/src")

    if(NOT EXISTS "${X264_PREFIX}/lib/libx264.a")
        message(STATUS "  x264 library not found in cache, will build from source")

        file(MAKE_DIRECTORY "${X264_BUILD_DIR}")
        file(MAKE_DIRECTORY "${X264_SOURCE_DIR}")

        # Download x264 source
        set(X264_TARBALL "${X264_BUILD_DIR}/x264.tar.bz2")
        if(NOT EXISTS "${X264_TARBALL}")
            message(STATUS "  Downloading x264...")
            file(DOWNLOAD
                "https://code.videolan.org/videolan/x264/-/archive/${X264_COMMIT}/x264-${X264_COMMIT}.tar.bz2"
                "${X264_TARBALL}"
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
                COMMAND ${CMAKE_COMMAND} -E tar xjf "${X264_TARBALL}"
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

        # Build x264 with musl
        message(STATUS "  Configuring x264...")

        include(ProcessorCount)
        ProcessorCount(NPROC)
        if(NPROC EQUAL 0)
            set(NPROC 4)
        endif()

        # x264 uses autotools — pass musl-gcc as CC with appropriate flags
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env
                CC=${MUSL_GCC}
                REALGCC=${REAL_GCC}
                CFLAGS=${MUSL_KERNEL_CFLAGS}\ -fPIC
                "${X264_SOURCE_DIR}/configure"
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
            WORKING_DIRECTORY "${X264_SOURCE_DIR}"
            RESULT_VARIABLE CONFIG_RESULT
            OUTPUT_VARIABLE CONFIG_OUTPUT
            ERROR_VARIABLE CONFIG_ERROR
        )
        if(NOT CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure x264:\n${CONFIG_OUTPUT}\n${CONFIG_ERROR}")
        endif()

        message(STATUS "  Building x264...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env REALGCC=${REAL_GCC} make -j${NPROC}
            WORKING_DIRECTORY "${X264_SOURCE_DIR}"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build x264:\n${BUILD_OUTPUT}\n${BUILD_ERROR}")
        endif()

        message(STATUS "  Installing x264...")
        execute_process(
            COMMAND make install
            WORKING_DIRECTORY "${X264_SOURCE_DIR}"
            RESULT_VARIABLE INSTALL_RESULT
            OUTPUT_VARIABLE INSTALL_OUTPUT
            ERROR_VARIABLE INSTALL_ERROR
        )
        if(NOT INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to install x264:\n${INSTALL_ERROR}")
        endif()

        message(STATUS "  ${BoldGreen}x264${ColorReset} built and installed successfully")
    else()
        message(STATUS "  ${BoldGreen}x264${ColorReset} library found in cache: ${BoldMagenta}${X264_PREFIX}/lib/libx264.a${ColorReset}")
    endif()

    set(X264_LIBRARIES "${X264_PREFIX}/lib/libx264.a")
    set(X264_INCLUDE_DIRS "${X264_PREFIX}/include")

    file(MAKE_DIRECTORY "${X264_PREFIX}/include" "${X264_PREFIX}/lib")

    if(NOT TARGET x264::x264)
        add_library(x264::x264 STATIC IMPORTED GLOBAL)
        set_target_properties(x264::x264 PROPERTIES
            IMPORTED_LOCATION "${X264_PREFIX}/lib/libx264.a"
            INTERFACE_INCLUDE_DIRECTORIES "${X264_PREFIX}/include"
        )
    endif()

    set(X264_FOUND TRUE)
    message(STATUS "${BoldGreen}✓${ColorReset} x264 (musl): ${X264_PREFIX}/lib/libx264.a")
    return()
endif()

# =============================================================================
# Non-musl: Use system package via pkg-config
# =============================================================================
include(FindPkgConfig)
pkg_check_modules(X264 REQUIRED x264)

if(NOT TARGET x264::x264)
    add_library(x264::x264 INTERFACE IMPORTED)
    target_include_directories(x264::x264 SYSTEM INTERFACE ${X264_INCLUDE_DIRS})
    target_link_libraries(x264::x264 INTERFACE ${X264_LIBRARIES})
endif()

set(X264_FOUND TRUE)
message(STATUS "${BoldGreen}✓${ColorReset} x264 found")
