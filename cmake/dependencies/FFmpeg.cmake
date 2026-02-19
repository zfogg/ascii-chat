# =============================================================================
# FFmpeg Library Configuration
# =============================================================================
# Find FFmpeg libraries for media file decoding

# Skip pkg-config search if using musl (libraries are built from source in MuslDependencies.cmake)
if(USE_MUSL)
    # MuslDependencies.cmake will set FFMPEG_FOUND and FFMPEG_LINK_LIBRARIES
    if(FFMPEG_FOUND)
        message(STATUS "${BoldGreen}✓${ColorReset} FFmpeg (musl): using musl-built static libraries")
        return()
    endif()
endif()

# =============================================================================
# Windows: Use vcpkg-provided FFmpeg
# =============================================================================
if(WIN32)
    # vcpkg integrates with CMake and provides FFmpeg via find_package
    # vcpkg's FindFFMPEG.cmake sets: FFMPEG_LIBRARIES, FFMPEG_INCLUDE_DIRS, FFMPEG_LIBRARY_DIRS
    find_package(FFMPEG QUIET)

    if(FFMPEG_FOUND)
        # vcpkg sets these variables:
        # - FFMPEG_LIBRARIES: list of library paths
        # - FFMPEG_INCLUDE_DIRS: include directories
        # - FFMPEG_LIBRARY_DIRS: library search paths

        # Set link libraries for target_link_libraries
        set(FFMPEG_LINK_LIBRARIES ${FFMPEG_LIBRARIES})

        message(STATUS "${BoldGreen}✓${ColorReset} FFmpeg found (Windows vcpkg)")
        message(STATUS "  - FFMPEG_INCLUDE_DIRS: ${FFMPEG_INCLUDE_DIRS}")
        message(STATUS "  - FFMPEG_LIBRARY_DIRS: ${FFMPEG_LIBRARY_DIRS}")
        message(STATUS "  - FFMPEG_LIBRARIES: ${FFMPEG_LIBRARIES}")
        return()
    else()
        message(WARNING "FFmpeg not found via vcpkg - media file streaming will be disabled")
        message(STATUS "Make sure ffmpeg is in vcpkg.json and vcpkg install has been run")
        set(FFMPEG_FOUND FALSE)
        return()
    endif()
endif()

# =============================================================================
# Release builds: Build FFmpeg from source when ASCIICHAT_SHARED_DEPS is OFF
# =============================================================================
# For portable Release builds (non-musl), compile FFmpeg from source with only
# built-in codecs (no external codec dependencies like svt-av1, dav1d, x264, x265, etc.)
# Musl builds always compile everything from source via MuslDependencies.cmake
# =============================================================================
if(NOT USE_MUSL AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_SHARED_DEPS)
    include(ProcessorCount)
    ProcessorCount(NPROC)
    if(NPROC EQUAL 0)
        set(NPROC 4)
    endif()

    set(FFMPEG_VERSION "7.1")
    set(FFMPEG_PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/ffmpeg")
    set(FFMPEG_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/ffmpeg-build")
    set(FFMPEG_SOURCE_DIR "${FFMPEG_BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}")

    # Build FFmpeg if not cached
    if(NOT EXISTS "${FFMPEG_PREFIX}/lib/libavformat.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libavcodec.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libavutil.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libswscale.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libswresample.a")

        message(STATUS "${BoldYellow}FFmpeg${ColorReset} not found in cache, building from source...")
        message(STATUS "  This takes about 1 minute on first build...")

        file(MAKE_DIRECTORY "${FFMPEG_BUILD_DIR}")

        # Download FFmpeg source
        set(FFMPEG_TARBALL "${FFMPEG_BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}.tar.xz")
        if(NOT EXISTS "${FFMPEG_TARBALL}")
            message(STATUS "  Downloading FFmpeg ${FFMPEG_VERSION}...")
            file(DOWNLOAD
                "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
                "${FFMPEG_TARBALL}"
                EXPECTED_HASH SHA256=40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6
                STATUS DOWNLOAD_STATUS
            )
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                message(FATAL_ERROR "Failed to download FFmpeg: ${ERROR_MSG}")
            endif()
        endif()

        # Extract tarball
        if(NOT EXISTS "${FFMPEG_SOURCE_DIR}/configure")
            message(STATUS "  Extracting FFmpeg...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xJf "${FFMPEG_TARBALL}"
                WORKING_DIRECTORY "${FFMPEG_BUILD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract FFmpeg tarball")
            endif()
        endif()

        # Configure FFmpeg with minimal build (only built-in codecs, no external deps)
        message(STATUS "  Configuring FFmpeg...")

        # Determine architecture-specific flags
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
            # x86_64: Disable asm to avoid nasm/yasm dependency
            set(FFMPEG_ARCH_FLAGS --disable-asm)
            message(STATUS "  Architecture: x86_64 (disabling asm)")
        else()
            set(FFMPEG_ARCH_FLAGS "")
            message(STATUS "  Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
        endif()

        # Pass through optimization flags for FFmpeg
        # Note: FFmpeg has issues with ThinLTO on macOS (stack probing conflicts),
        # so we use standard optimizations instead. The main binary still uses LTO.
        set(FFMPEG_CFLAGS "-fPIC -O3")

        execute_process(
            COMMAND "${FFMPEG_SOURCE_DIR}/configure"
                --prefix=${FFMPEG_PREFIX}
                --enable-static
                --disable-shared
                --enable-pic
                --extra-cflags=${FFMPEG_CFLAGS}
                --disable-programs
                --disable-doc
                --disable-htmlpages
                --disable-manpages
                --disable-podpages
                --disable-txtpages
                --disable-debug
                --disable-autodetect
                --enable-videotoolbox
                --enable-audiotoolbox
                --enable-protocol=file
                --enable-demuxer=mov,matroska,avi,gif,image2,mp3,wav,flac,ogg
                --enable-decoder=h264,hevc,vp8,vp9,av1,mpeg4,png,gif,mjpeg,mp3,aac,flac,vorbis,opus,pcm_s16le
                --enable-parser=h264,hevc,vp8,vp9,av1,mpeg4video,aac,mpegaudio
                --enable-swscale
                --enable-swresample
                --enable-zlib
                --enable-bzlib
                ${FFMPEG_ARCH_FLAGS}
            WORKING_DIRECTORY "${FFMPEG_SOURCE_DIR}"
            RESULT_VARIABLE CONFIG_RESULT
            OUTPUT_VARIABLE CONFIG_OUTPUT
            ERROR_VARIABLE CONFIG_ERROR
        )
        if(NOT CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure FFmpeg:\nstdout: ${CONFIG_OUTPUT}\nstderr: ${CONFIG_ERROR}")
        endif()

        # Build FFmpeg
        message(STATUS "  Building FFmpeg (using ${NPROC} jobs)...")
        execute_process(
            COMMAND make -j${NPROC}
            WORKING_DIRECTORY "${FFMPEG_SOURCE_DIR}"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build FFmpeg:\n${BUILD_ERROR}")
        endif()

        # Install FFmpeg
        message(STATUS "  Installing FFmpeg to cache...")
        execute_process(
            COMMAND make install
            WORKING_DIRECTORY "${FFMPEG_SOURCE_DIR}"
            RESULT_VARIABLE INSTALL_RESULT
            OUTPUT_VARIABLE INSTALL_OUTPUT
            ERROR_VARIABLE INSTALL_ERROR
        )
        if(NOT INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to install FFmpeg:\n${INSTALL_ERROR}")
        endif()

        message(STATUS "  ${BoldGreen}FFmpeg${ColorReset} built and cached successfully")
    else()
        message(STATUS "${BoldGreen}FFmpeg${ColorReset} found in cache: ${FFMPEG_PREFIX}/lib/libavformat.a")
    endif()

    # Set FFmpeg variables
    set(FFMPEG_FOUND TRUE)
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")

    # Static FFmpeg libraries + macOS frameworks
    set(FFMPEG_LIBRARIES
        "${FFMPEG_PREFIX}/lib/libavformat.a"
        "${FFMPEG_PREFIX}/lib/libavcodec.a"
        "${FFMPEG_PREFIX}/lib/libswscale.a"
        "${FFMPEG_PREFIX}/lib/libswresample.a"
        "${FFMPEG_PREFIX}/lib/libavutil.a"
        "-framework AudioToolbox"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework VideoToolbox"
        "-framework Security"
        "-framework CoreFoundation"
        "-framework CoreServices"
        "-liconv"
        "-lbz2"
        "-lz"
    )

    # Set FFMPEG_LINK_LIBRARIES for target_link_libraries
    set(FFMPEG_LINK_LIBRARIES ${FFMPEG_LIBRARIES})

    message(STATUS "${BoldGreen}✓${ColorReset} FFmpeg configured (static from source):")
    message(STATUS "  - libavformat: ${FFMPEG_PREFIX}/lib/libavformat.a")
    message(STATUS "  - libavcodec: ${FFMPEG_PREFIX}/lib/libavcodec.a")
    message(STATUS "  - libavutil: ${FFMPEG_PREFIX}/lib/libavutil.a")
    message(STATUS "  - libswscale: ${FFMPEG_PREFIX}/lib/libswscale.a")
    message(STATUS "  - libswresample: ${FFMPEG_PREFIX}/lib/libswresample.a")
endif()

# =============================================================================
# Fallback: Use pkg-config (dynamic linking)
# =============================================================================
# Check if we need to run pkg-config: either not found yet, or found but no link libs set
if(NOT FFMPEG_FOUND OR NOT FFMPEG_LINK_LIBRARIES)
    message(STATUS "FFmpeg: Searching via pkg-config...")
    # Find pkg-config (required for finding FFmpeg)
    find_package(PkgConfig QUIET)

    if(NOT PkgConfig_FOUND)
        message(WARNING "pkg-config not found - FFmpeg support will be disabled")
        set(FFMPEG_FOUND FALSE)
        return()
    endif()

    # Find FFmpeg components
    pkg_check_modules(FFMPEG
        libavformat
        libavcodec
        libavutil
        libswscale
        libswresample
    )

    if(NOT FFMPEG_FOUND)
        message(WARNING "FFmpeg not found - media file streaming will be disabled")
        message(STATUS "Install FFmpeg development libraries:")
        message(STATUS "  Debian/Ubuntu: sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev")
        message(STATUS "  Fedora/RHEL: sudo dnf install ffmpeg-devel")
        message(STATUS "  Arch Linux: sudo pacman -S ffmpeg")
        message(STATUS "  macOS: brew install ffmpeg")
        set(FFMPEG_FOUND FALSE)
        return()
    endif()

    # Set FFMPEG_LINK_LIBRARIES for target_link_libraries (matches macOS static build)
    # Use FFMPEG_LDFLAGS which includes -L path and -l flags, not just library names
    set(FFMPEG_LINK_LIBRARIES ${FFMPEG_LDFLAGS})

    message(STATUS "${BoldGreen}✓${ColorReset} FFmpeg found:")
    message(STATUS "  - libavformat: ${FFMPEG_libavformat_VERSION}")
    message(STATUS "  - libavcodec: ${FFMPEG_libavcodec_VERSION}")
    message(STATUS "  - libavutil: ${FFMPEG_libavutil_VERSION}")
    message(STATUS "  - libswscale: ${FFMPEG_libswscale_VERSION}")
    message(STATUS "  - libswresample: ${FFMPEG_libswresample_VERSION}")
    message(STATUS "  - FFMPEG_LINK_LIBRARIES: ${FFMPEG_LINK_LIBRARIES}")
endif()

# Variables FFMPEG_LIBRARIES, FFMPEG_INCLUDE_DIRS, and FFMPEG_FOUND
# are set and are available in parent scope
# SourceFiles.cmake will conditionally include media source files when FFMPEG_FOUND is true
