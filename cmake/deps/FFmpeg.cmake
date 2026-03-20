# =============================================================================
# FFmpeg Library Configuration
# =============================================================================
# Find FFmpeg libraries for media file decoding
#
# For iOS builds: Built from source with iOS cross-compilation
# For musl builds: Built from source at configure time
# For Release builds: Built from source with minimal codec set
# For other builds: Uses system package manager (pkg-config)

set(FFMPEG_VERSION "7.1.3")
set(FFMPEG_HASH "f0bf043299db9e3caacb435a712fc541fbb07df613c4b893e8b77e67baf3adbe")

# iOS build: Build from source for iOS cross-compilation
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}FFmpeg${ColorReset} from source (iOS cross-compile)...")

    include(ExternalProject)

    set(FFMPEG_PREFIX "${IOS_DEPS_CACHE_DIR}/ffmpeg")
    set(FFMPEG_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/ffmpeg-build")

    # Get actual iOS SDK path using xcrun
    if(BUILD_IOS_SIM)
        execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(IOS_SDK_NAME "iphonesimulator")
    else()
        execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(IOS_SDK_NAME "iphoneos")
    endif()

    if(NOT EXISTS "${FFMPEG_PREFIX}/lib/libavformat.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libavcodec.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libavutil.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libswscale.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libswresample.a")

        message(STATUS "  FFmpeg libraries not found in cache, will build from source")

        ExternalProject_Add(ffmpeg-ios
            URL https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz
            URL_HASH SHA256=${FFMPEG_HASH}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${FFMPEG_BUILD_DIR}
            STAMP_DIR ${FFMPEG_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND bash -c "cd <SOURCE_DIR> && ./configure \
                --prefix=${FFMPEG_PREFIX} \
                --enable-cross-compile \
                --arch=arm64 \
                --cc=clang \
                --sysroot=${IOS_SDK_PATH} \
                --enable-static \
                --disable-shared \
                --enable-pic \
                --disable-programs \
                --disable-doc \
                --disable-htmlpages \
                --disable-manpages \
                --disable-podpages \
                --disable-txtpages \
                --disable-debug \
                --disable-autodetect \
                --disable-asm \
                --disable-inline-asm \
                --enable-gpl \
                --enable-protocol=file,http,https,rtsp,rtmp,hls \
                --enable-demuxer=mov,matroska,avi,gif,image2,mp3,wav,flac,ogg,hls \
                --enable-decoder=h264,hevc,vp8,vp9,av1,mpeg4,png,gif,mjpeg,mp3,aac,flac,vorbis,opus,pcm_s16le \
                --enable-encoder=hevc_videotoolbox,libx265 \
                --enable-libx265 \
                --enable-parser=h264,hevc,vp8,vp9,av1,mpeg4video,aac,mpegaudio \
                --enable-swscale \
                --enable-swresample"
            BUILD_COMMAND bash -c "cd <SOURCE_DIR> && make -j"
            INSTALL_COMMAND bash -c "cd <SOURCE_DIR> && make install"
            BUILD_BYPRODUCTS
                ${FFMPEG_PREFIX}/lib/libavformat.a
                ${FFMPEG_PREFIX}/lib/libavcodec.a
                ${FFMPEG_PREFIX}/lib/libavutil.a
                ${FFMPEG_PREFIX}/lib/libswscale.a
                ${FFMPEG_PREFIX}/lib/libswresample.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}FFmpeg${ColorReset} libraries found in cache: ${BoldMagenta}${FFMPEG_PREFIX}/lib/libavformat.a${ColorReset}")
        add_custom_target(ffmpeg-ios)
    endif()

    set(FFMPEG_FOUND TRUE)
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")

    # iOS frameworks needed for FFmpeg
    set(FFMPEG_LIBRARIES
        "${FFMPEG_PREFIX}/lib/libavformat.a"
        "${FFMPEG_PREFIX}/lib/libavcodec.a"
        "${FFMPEG_PREFIX}/lib/libavutil.a"
        "${FFMPEG_PREFIX}/lib/libswscale.a"
        "${FFMPEG_PREFIX}/lib/libswresample.a"
        "-framework CoreFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework VideoToolbox"
        "-framework AudioToolbox"
        "-framework Security"
        "-liconv"
    )

    message(STATUS "${BoldGreen}✓${ColorReset} FFmpeg configured (iOS cross-compile):")
    message(STATUS "  - libavformat: ${FFMPEG_PREFIX}/lib/libavformat.a")
    message(STATUS "  - libavcodec: ${FFMPEG_PREFIX}/lib/libavcodec.a")
    message(STATUS "  - libavutil: ${FFMPEG_PREFIX}/lib/libavutil.a")
    message(STATUS "  - libswscale: ${FFMPEG_PREFIX}/lib/libswscale.a")
    message(STATUS "  - libswresample: ${FFMPEG_PREFIX}/lib/libswresample.a")

    return()
endif()

# Handle musl builds - FFmpeg is built from source at configure time
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}FFmpeg${ColorReset} from source...")

    set(FFMPEG_PREFIX "${MUSL_DEPS_DIR_STATIC}/ffmpeg")
    set(FFMPEG_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/ffmpeg-build")
    set(FFMPEG_SOURCE_DIR "${FFMPEG_BUILD_DIR}/src/ffmpeg")

    # Build FFmpeg synchronously at configure time if not cached
    if(NOT EXISTS "${FFMPEG_PREFIX}/lib/libavformat.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libavcodec.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libavutil.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libswscale.a" OR
       NOT EXISTS "${FFMPEG_PREFIX}/lib/libswresample.a")
        message(STATUS "  FFmpeg libraries not found in cache, will build from source")
        message(STATUS "  This may take several minutes on first build...")

        file(MAKE_DIRECTORY "${FFMPEG_BUILD_DIR}")
        file(MAKE_DIRECTORY "${FFMPEG_SOURCE_DIR}")

        # Download FFmpeg source
        set(FFMPEG_TARBALL "${FFMPEG_BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}.tar.xz")
        if(NOT EXISTS "${FFMPEG_TARBALL}")
            message(STATUS "  Downloading FFmpeg ${FFMPEG_VERSION}...")
            file(DOWNLOAD
                "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
                "${FFMPEG_TARBALL}"
                EXPECTED_HASH SHA256=${FFMPEG_HASH}
                STATUS DOWNLOAD_STATUS
                SHOW_PROGRESS
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
            # Move from ffmpeg-${FFMPEG_VERSION}/ to src/ffmpeg/
            file(RENAME "${FFMPEG_BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}" "${FFMPEG_SOURCE_DIR}")
        endif()

        # Configure FFmpeg
        message(STATUS "  Configuring FFmpeg...")
        # Find GCC's lib directories for libstdc++/libgcc (needed for x265 C++ link test)
        execute_process(
            COMMAND ${REAL_GCC} -print-file-name=libgcc.a
            OUTPUT_VARIABLE _GCC_LIBGCC_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(
            COMMAND ${REAL_GCC} -print-file-name=libstdc++.a
            OUTPUT_VARIABLE _GCC_LIBSTDCXX_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        get_filename_component(GCC_LIB_DIR "${_GCC_LIBGCC_PATH}" DIRECTORY)
        get_filename_component(GCC_STDCXX_DIR "${_GCC_LIBSTDCXX_PATH}" REALPATH)
        get_filename_component(GCC_STDCXX_DIR "${GCC_STDCXX_DIR}" DIRECTORY)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env
                CC=${MUSL_GCC}
                REALGCC=${REAL_GCC}
                CFLAGS=${MUSL_KERNEL_CFLAGS}\ -fPIC
                PKG_CONFIG_PATH=${X265_PREFIX}/lib/pkgconfig
                ASAN_OPTIONS=
                "${FFMPEG_SOURCE_DIR}/configure"
                --prefix=${FFMPEG_PREFIX}
                --cc=${MUSL_GCC}
                --extra-cflags=-I${X265_PREFIX}/include
                "--extra-ldflags=-L${X265_PREFIX}/lib -L${GCC_STDCXX_DIR} -L${GCC_LIB_DIR}"
                "--extra-libs=-lm -Wl,-Bstatic -lstdc++ -lgcc -lgcc_eh -Wl,-Bdynamic"
                --enable-static
                --disable-shared
                --enable-pic
                --disable-programs
                --disable-doc
                --disable-htmlpages
                --disable-manpages
                --disable-podpages
                --disable-txtpages
                --disable-debug
                --disable-runtime-cpudetect
                --disable-autodetect
                --disable-x86asm
                --disable-inline-asm
                --enable-gpl
                --enable-protocol=file
                --enable-demuxer=mov,matroska,avi,gif,image2
                --enable-decoder=h264,hevc,vp8,vp9,av1,mpeg4,png,gif,mjpeg
                --enable-encoder=libx265
                --enable-libx265
                --enable-parser=h264,hevc,vp8,vp9,av1,mpeg4video
                --enable-swscale
                --enable-swresample
            WORKING_DIRECTORY "${FFMPEG_SOURCE_DIR}"
            RESULT_VARIABLE CONFIG_RESULT
            OUTPUT_VARIABLE CONFIG_OUTPUT
            ERROR_VARIABLE CONFIG_ERROR
        )
        if(NOT CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure FFmpeg:\n${CONFIG_OUTPUT}\n${CONFIG_ERROR}")
        endif()

        # Build FFmpeg
        message(STATUS "  Building FFmpeg (this takes several minutes)...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env REALGCC=${REAL_GCC} CFLAGS=${MUSL_KERNEL_CFLAGS}\ -fPIC make -j${CMAKE_BUILD_PARALLEL_LEVEL}
            WORKING_DIRECTORY "${FFMPEG_SOURCE_DIR}"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build FFmpeg:\n${BUILD_ERROR}")
        endif()

        # Install FFmpeg
        message(STATUS "  Installing FFmpeg...")
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
        message(STATUS "  ${BoldBlue}FFmpeg${ColorReset} libraries found in cache: ${BoldMagenta}${FFMPEG_PREFIX}/lib/libavformat.a${ColorReset}")
    endif()

    # Set FFmpeg variables for use in the build
    set(FFMPEG_FOUND TRUE)
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include")

    set(FFMPEG_LIBRARIES
        "${FFMPEG_PREFIX}/lib/libavformat.a"
        "${FFMPEG_PREFIX}/lib/libavcodec.a"
        "${FFMPEG_PREFIX}/lib/libavutil.a"
        "${FFMPEG_PREFIX}/lib/libswscale.a"
        "${FFMPEG_PREFIX}/lib/libswresample.a"
        "${X265_PREFIX}/lib/libx265.a")

    # Create a custom target for musl builds (libraries are pre-built/cached)
    add_custom_target(ffmpeg-musl)

    return()
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
                EXPECTED_HASH SHA256=${FFMPEG_HASH}
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

        # Platform-specific hardware acceleration flags
        if(APPLE)
            set(FFMPEG_HWACCEL_FLAGS --enable-videotoolbox --enable-audiotoolbox)
        else()
            set(FFMPEG_HWACCEL_FLAGS --disable-videotoolbox --disable-audiotoolbox)
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
                --enable-gpl
                ${FFMPEG_HWACCEL_FLAGS}
                --enable-protocol=file
                --enable-demuxer=mov,matroska,avi,gif,image2,mp3,wav,flac,ogg
                --enable-decoder=h264,hevc,vp8,vp9,av1,mpeg4,png,gif,mjpeg,mp3,aac,flac,vorbis,opus,pcm_s16le
                --enable-encoder=libx265
                --enable-libx265
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

    # Static FFmpeg libraries + platform-specific dependencies
    set(FFMPEG_LIBRARIES
        "${FFMPEG_PREFIX}/lib/libavformat.a"
        "${FFMPEG_PREFIX}/lib/libavcodec.a"
        "${FFMPEG_PREFIX}/lib/libswscale.a"
        "${FFMPEG_PREFIX}/lib/libswresample.a"
        "${FFMPEG_PREFIX}/lib/libavutil.a"
    )

    # Add macOS frameworks only on macOS
    if(APPLE)
        list(APPEND FFMPEG_LIBRARIES
            "-framework AudioToolbox"
            "-framework CoreMedia"
            "-framework CoreVideo"
            "-framework VideoToolbox"
            "-framework Security"
            "-framework CoreFoundation"
            "-framework CoreServices"
            "-liconv"
        )
    else()
        # On Linux/Unix, add compression libraries (iconv is part of glibc, don't link it separately)
        list(APPEND FFMPEG_LIBRARIES "-lbz2" "-lz")
    endif()


    message(STATUS "${BoldGreen}✓${ColorReset} FFmpeg configured (static from source):")
    message(STATUS "  - libavformat: ${FFMPEG_PREFIX}/lib/libavformat.a")
    message(STATUS "  - libavcodec: ${FFMPEG_PREFIX}/lib/libavcodec.a")
    message(STATUS "  - libavutil: ${FFMPEG_PREFIX}/lib/libavutil.a")
    message(STATUS "  - libswscale: ${FFMPEG_PREFIX}/lib/libswscale.a")
    message(STATUS "  - libswresample: ${FFMPEG_PREFIX}/lib/libswresample.a")
endif()

# =============================================================================
# Debug builds: Use system FFmpeg for fast development iteration
# =============================================================================
# Debug builds skip the built FFmpeg and use the system version via pkg-config
# This allows much faster compilation during development without the overhead
# of building FFmpeg from source. If system FFmpeg is not available, we'll fall
# back to pkg-config search below.
if(NOT USE_MUSL AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT FFMPEG_FOUND)
    message(STATUS "Debug build: ${BoldBlue}FFmpeg${ColorReset} will use system libraries (fast development iteration)")
endif()

# =============================================================================
# Fallback: Use pkg-config (dynamic linking)
# =============================================================================
# Check if we need to run pkg-config: either not found yet, or found but no libraries set
if(NOT FFMPEG_FOUND OR NOT FFMPEG_LIBRARIES)
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

    # pkg_check_modules sets FFMPEG_LIBRARIES (library names), FFMPEG_LIBRARY_DIRS (paths), and FFMPEG_LDFLAGS (full flags)
    # We use FFMPEG_LINK_LIBRARIES which pkg-config creates from the above and is the proper way to link
    # If not available, fall back to FFMPEG_LDFLAGS which includes all necessary -L and -l flags
    if(FFMPEG_LINK_LIBRARIES)
        set(FFMPEG_LIBRARIES ${FFMPEG_LINK_LIBRARIES})
    else()
        # Fallback to full linker flags (includes both -L and -l flags)
        set(FFMPEG_LIBRARIES ${FFMPEG_LDFLAGS})
    endif()

    # Store library directories for later use if needed
    set(FFMPEG_LIBRARY_DIRS ${FFMPEG_LIBRARY_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} FFmpeg found:")
    message(STATUS "  - libavformat: ${FFMPEG_libavformat_VERSION}")
    message(STATUS "  - libavcodec: ${FFMPEG_libavcodec_VERSION}")
    message(STATUS "  - libavutil: ${FFMPEG_libavutil_VERSION}")
    message(STATUS "  - libswscale: ${FFMPEG_libswscale_VERSION}")
    message(STATUS "  - libswresample: ${FFMPEG_libswresample_VERSION}")
    message(STATUS "  - FFMPEG_LINK_LIBRARIES: ${FFMPEG_LINK_LIBRARIES}")
endif()

# Variables FFMPEG_LIBRARIES, FFMPEG_INCLUDE_DIRS, and FFMPEG_FOUND are available
# SourceFiles.cmake will conditionally include media source files when FFMPEG_FOUND is true
