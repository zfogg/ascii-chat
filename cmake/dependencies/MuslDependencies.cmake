# =============================================================================
# MuslDependencies.cmake - Build all dependencies from source for musl
# =============================================================================
# When USE_MUSL=ON, we can't use system libraries (glibc-based). Instead, we
# build all dependencies from source and cache them in .deps-cache-musl/.
#
# Dependencies built from source:
#   - zstd (compression)
#   - libsodium (crypto)
#   - OpenSSL (TLS/SSL for libdatachannel TURN credentials)
#   - PortAudio (audio I/O)
#   - Opus (audio codec)
#   - BearSSL (TLS for SSH key fetching)
#   - PCRE2 (regular expressions for URL validation)
#
# All cached in ${FETCHCONTENT_BASE_DIR} to persist across build/ deletions.
# =============================================================================

if(NOT USE_MUSL)
    return()
endif()

# =============================================================================
# Validate required programs for musl builds
# =============================================================================
# musl-gcc and gcc are required to build dependencies from source
if(NOT ASCIICHAT_MUSL_GCC_EXECUTABLE)
    message(FATAL_ERROR "musl-gcc not found. Required for Linux Release builds with USE_MUSL=ON.\n"
        "Install musl development tools:\n"
        "  Arch Linux: sudo pacman -S musl\n"
        "  Ubuntu/Debian: sudo apt install musl-tools\n"
        "  Fedora: sudo dnf install musl-gcc")
endif()

if(NOT ASCIICHAT_GCC_EXECUTABLE)
    message(FATAL_ERROR "gcc not found. Required by musl-gcc (via REALGCC).\n"
        "Install GCC:\n"
        "  Arch Linux: sudo pacman -S gcc\n"
        "  Ubuntu/Debian: sudo apt install gcc\n"
        "  Fedora: sudo dnf install gcc")
endif()

# Set variables for use in ExternalProject commands
set(MUSL_GCC "${ASCIICHAT_MUSL_GCC_EXECUTABLE}")
set(REAL_GCC "${ASCIICHAT_GCC_EXECUTABLE}")

message(STATUS "Building dependencies from source for musl libc...")
message(STATUS "  musl-gcc: ${MUSL_GCC}")
message(STATUS "  gcc (REALGCC): ${REAL_GCC}")

include(FetchContent)

# Save current output directories
set(_SAVED_ARCHIVE_OUTPUT_DIR ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
set(_SAVED_LIBRARY_OUTPUT_DIR ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

# Use cache directory for musl deps - separate directory for each dependency
# MUSL_DEPS_DIR_STATIC is set in Musl.cmake's configure_musl_post_project()
# Each dependency gets its own subdirectory for cleaner organization
message(STATUS "MuslDependencies.cmake: MUSL_DEPS_DIR_STATIC=${MUSL_DEPS_DIR_STATIC}")

# Validate that MUSL_DEPS_DIR_STATIC is properly set (not empty or root path)
if(NOT MUSL_DEPS_DIR_STATIC OR MUSL_DEPS_DIR_STATIC STREQUAL "" OR MUSL_DEPS_DIR_STATIC STREQUAL "/")
    message(FATAL_ERROR "MUSL_DEPS_DIR_STATIC is not properly set (value: '${MUSL_DEPS_DIR_STATIC}'). "
                        "This usually means CMAKE_BUILD_TYPE was empty when ASCIICHAT_DEPS_CACHE_* was configured. "
                        "Please specify -DCMAKE_BUILD_TYPE=Release (or Debug) on the command line.")
endif()

# =============================================================================
# Copy kernel headers to project-local directory
# =============================================================================
# Clang with musl doesn't include kernel headers by default. We need linux/, asm/,
# and asm-generic/ headers for ALSA and V4L2. Copy them to a project-local
# directory so the build works on fresh installs without system modifications.

set(KERNEL_HEADERS_DIR "${MUSL_DEPS_DIR_STATIC}/kernel-headers")

# Find kernel headers from common locations
set(KERNEL_HEADER_SEARCH_PATHS
    "/usr/include/linux"
    "/usr/include/x86_64-linux-gnu/asm"
    "/usr/include/asm"
    "/usr/include/asm-generic"
)

# Check if kernel headers exist
foreach(HEADER_PATH ${KERNEL_HEADER_SEARCH_PATHS})
    if(EXISTS "${HEADER_PATH}")
        break()
    endif()
endforeach()

if(KERNEL_HEADERS_FOUND)
    # Copy kernel headers only if they don't exist in cache
    if(NOT EXISTS "${KERNEL_HEADERS_DIR}/linux")
        message(STATUS "Copying kernel headers to ${KERNEL_HEADERS_DIR}...")
        file(MAKE_DIRECTORY "${KERNEL_HEADERS_DIR}")

        # Copy linux/ headers
        if(EXISTS "/usr/include/linux")
            file(COPY "/usr/include/linux" DESTINATION "${KERNEL_HEADERS_DIR}")
        endif()

        # Copy asm/ headers (try arch-specific first, then generic)
        # Detect architecture-specific header path
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
            set(_ASM_ARCH_PATH "/usr/include/aarch64-linux-gnu/asm")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set(_ASM_ARCH_PATH "/usr/include/x86_64-linux-gnu/asm")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i686|i386")
            set(_ASM_ARCH_PATH "/usr/include/i386-linux-gnu/asm")
        else()
            set(_ASM_ARCH_PATH "")
        endif()

        if(_ASM_ARCH_PATH AND EXISTS "${_ASM_ARCH_PATH}")
            file(COPY "${_ASM_ARCH_PATH}" DESTINATION "${KERNEL_HEADERS_DIR}")
        elseif(EXISTS "/usr/include/asm")
            file(COPY "/usr/include/asm" DESTINATION "${KERNEL_HEADERS_DIR}")
        else()
            # Fallback: create asm symlink to asm-generic (works for many headers)
            message(STATUS "No arch-specific asm headers found, creating symlink to asm-generic")
            file(CREATE_LINK "${KERNEL_HEADERS_DIR}/asm-generic" "${KERNEL_HEADERS_DIR}/asm" SYMBOLIC)
        endif()

        # Copy asm-generic/ headers
        if(EXISTS "/usr/include/asm-generic")
            file(COPY "/usr/include/asm-generic" DESTINATION "${KERNEL_HEADERS_DIR}")
        endif()

        message(STATUS "Kernel headers copied successfully")
    else()
        message(STATUS "Using cached ${BoldBlue}kernel headers${ColorReset} from ${BoldMagenta}${KERNEL_HEADERS_DIR}${ColorReset}")
    endif()

    # Set CFLAGS to include kernel headers for ALSA and PortAudio builds
    set(MUSL_KERNEL_CFLAGS "-fPIC -I${KERNEL_HEADERS_DIR}")
else()
    message(WARNING "Kernel headers not found in common locations. Install linux-libc-dev or kernel-headers package.")
endif()

# =============================================================================
# zstd - Compression library
# =============================================================================
message(STATUS "Configuring ${BoldBlue}zstd${ColorReset} from source...")

set(ZSTD_PREFIX "${MUSL_DEPS_DIR_STATIC}/zstd")
set(ZSTD_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/zstd-build")

# Only add external project if library doesn't exist
if(NOT EXISTS "${ZSTD_PREFIX}/lib/libzstd.a")
    message(STATUS "  zstd library not found in cache, will build from source")
    ExternalProject_Add(zstd-musl
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${ZSTD_BUILD_DIR}
        STAMP_DIR ${ZSTD_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND ""
        BUILD_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j -C <SOURCE_DIR> lib-release PREFIX=${ZSTD_PREFIX}
        INSTALL_COMMAND make -C <SOURCE_DIR> install PREFIX=${ZSTD_PREFIX}
        BUILD_IN_SOURCE 1
        BUILD_BYPRODUCTS ${ZSTD_PREFIX}/lib/libzstd.a
        LOG_DOWNLOAD TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}zstd${ColorReset} library found in cache: ${BoldMagenta}${ZSTD_PREFIX}/lib/libzstd.a${ColorReset}")
    # Create a dummy target so dependencies can reference it
    add_custom_target(zstd-musl)
endif()

set(ZSTD_LIBRARIES "${ZSTD_PREFIX}/lib/libzstd.a")
set(ZSTD_INCLUDE_DIRS "${ZSTD_PREFIX}/include")

# Create placeholder directories so CMake validation doesn't fail at configure time
file(MAKE_DIRECTORY "${ZSTD_PREFIX}/include")

# Create imported target for zstd to match system find_package behavior
if(NOT TARGET zstd::libzstd)
    add_library(zstd::libzstd STATIC IMPORTED GLOBAL)
    set_target_properties(zstd::libzstd PROPERTIES
        IMPORTED_LOCATION "${ZSTD_PREFIX}/lib/libzstd.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_PREFIX}/include"
    )
endif()

# =============================================================================
# zlib - Compression library (required by libwebsockets)
# =============================================================================
set(ZLIB_PREFIX "${MUSL_DEPS_DIR_STATIC}/zlib")
set(ZLIB_INCLUDE_DIR "${ZLIB_PREFIX}/include")
set(ZLIB_LIBRARY "${ZLIB_PREFIX}/lib/libz.a")

if(NOT EXISTS "${ZLIB_LIBRARY}")
    message(STATUS "  zlib library not found in cache, will build from source")

    # Download zlib source manually first - GitHub archive endpoint has non-deterministic hashes
    # So we download, verify, and let ExternalProject use the cached file
    set(ZLIB_DOWNLOAD_DIR "${MUSL_DEPS_DIR_STATIC}/zlib-src")
    set(ZLIB_TARBALL "${ZLIB_DOWNLOAD_DIR}/zlib-1.3.1.tar.gz")
    set(ZLIB_EXTRACTED "${ZLIB_DOWNLOAD_DIR}/zlib-1.3.1")

    file(MAKE_DIRECTORY "${ZLIB_DOWNLOAD_DIR}")

    if(NOT EXISTS "${ZLIB_EXTRACTED}")
        if(NOT EXISTS "${ZLIB_TARBALL}")
            message(STATUS "  Downloading zlib 1.3.1...")
            # Use official zlib source from GitHub releases (not archive endpoint)
            file(DOWNLOAD
                "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz"
                "${ZLIB_TARBALL}"
                TIMEOUT 30
                STATUS DOWNLOAD_STATUS
                SHOW_PROGRESS
            )
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                message(WARNING "Failed to download from releases endpoint: ${ERROR_MSG}")
                message(STATUS "  Attempting fallback: downloading from archive endpoint...")
                # Fallback to archive endpoint without hash verification
                file(DOWNLOAD
                    "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz"
                    "${ZLIB_TARBALL}"
                    TIMEOUT 30
                    STATUS DOWNLOAD_STATUS
                    SHOW_PROGRESS
                )
                list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
                if(NOT STATUS_CODE EQUAL 0)
                    list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                    message(FATAL_ERROR "Failed to download zlib from both endpoints: ${ERROR_MSG}")
                endif()
            endif()
        endif()

        message(STATUS "  Extracting zlib...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${ZLIB_TARBALL}"
            WORKING_DIRECTORY "${ZLIB_DOWNLOAD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
            OUTPUT_VARIABLE EXTRACT_OUTPUT
            ERROR_VARIABLE EXTRACT_ERROR
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract zlib: ${EXTRACT_ERROR}")
        endif()
    endif()

    ExternalProject_Add(zlib-musl
        SOURCE_DIR "${ZLIB_EXTRACTED}"
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        STAMP_DIR ${MUSL_DEPS_DIR_STATIC}/zlib-build/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} <SOURCE_DIR>/configure --prefix=${ZLIB_PREFIX} --static
        BUILD_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${ZLIB_LIBRARY}
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}zlib${ColorReset} library found in cache: ${BoldMagenta}${ZLIB_LIBRARY}${ColorReset}")
    add_custom_target(zlib-musl)
endif()

# =============================================================================
# libsodium - Cryptography library
# =============================================================================
message(STATUS "Configuring ${BoldBlue}libsodium${ColorReset} from source...")

set(LIBSODIUM_PREFIX "${MUSL_DEPS_DIR_STATIC}/libsodium")
set(LIBSODIUM_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libsodium-build")

# Always rebuild libsodium to ensure -fPIC compilation for shared library linking
# (ExternalProject cache check happens regardless, but this forces rebuild of external project)
ExternalProject_Add(libsodium-musl
    URL https://github.com/jedisct1/libsodium/releases/download/1.0.20-RELEASE/libsodium-1.0.20.tar.gz
    URL_HASH SHA256=ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX ${LIBSODIUM_BUILD_DIR}
    STAMP_DIR ${LIBSODIUM_BUILD_DIR}/stamps
    UPDATE_DISCONNECTED 1
    BUILD_ALWAYS 0
    # For shared library support, ALL object files must be compiled with -fPIC.
    # Use --with-pic to force position-independent code generation.
    CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} <SOURCE_DIR>/configure --prefix=${LIBSODIUM_PREFIX} --enable-static --disable-shared --with-pic
    BUILD_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} make -j
    INSTALL_COMMAND make install
    DEPENDS zstd-musl
    BUILD_BYPRODUCTS ${LIBSODIUM_PREFIX}/lib/libsodium.a
    LOG_DOWNLOAD TRUE
    LOG_CONFIGURE TRUE
    LOG_BUILD TRUE
    LOG_INSTALL TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

set(LIBSODIUM_LIBRARIES "${LIBSODIUM_PREFIX}/lib/libsodium.a")
set(LIBSODIUM_INCLUDE_DIRS "${LIBSODIUM_PREFIX}/include")

# =============================================================================
# OpenSSL - TLS/SSL library (needed for libdatachannel TURN credentials)
# =============================================================================
# OpenSSL MUST be built synchronously at configure time because libdatachannel's
# configure also runs at configure time (via execute_process in Libdatachannel.cmake).
# If we use ExternalProject (which runs at build time), OpenSSL won't exist when
# libdatachannel tries to find_package(OpenSSL).
# =============================================================================
message(STATUS "Configuring ${BoldBlue}OpenSSL${ColorReset} from source...")

set(OPENSSL_PREFIX "${MUSL_DEPS_DIR_STATIC}/openssl")
set(OPENSSL_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/openssl-build")
set(OPENSSL_SOURCE_DIR "${OPENSSL_BUILD_DIR}/src/openssl")

# Detect target architecture for OpenSSL Configure
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(OPENSSL_TARGET "linux-aarch64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(OPENSSL_TARGET "linux-x86_64")
else()
    set(OPENSSL_TARGET "linux-generic64")
endif()

# Build OpenSSL synchronously at configure time if not cached
# Note: OpenSSL 3.x installs to lib64/ by default on x86_64
if(NOT EXISTS "${OPENSSL_PREFIX}/lib64/libssl.a" OR NOT EXISTS "${OPENSSL_PREFIX}/lib64/libcrypto.a")
    message(STATUS "  OpenSSL library not found in cache, will build from source")
    message(STATUS "  This may take a few minutes on first build...")

    file(MAKE_DIRECTORY "${OPENSSL_BUILD_DIR}")
    file(MAKE_DIRECTORY "${OPENSSL_SOURCE_DIR}")

    # Download OpenSSL source
    set(OPENSSL_TARBALL "${OPENSSL_BUILD_DIR}/openssl-3.4.0.tar.gz")
    if(NOT EXISTS "${OPENSSL_TARBALL}")
        message(STATUS "  Downloading OpenSSL 3.4.0...")
        file(DOWNLOAD
            "https://github.com/openssl/openssl/releases/download/openssl-3.4.0/openssl-3.4.0.tar.gz"
            "${OPENSSL_TARBALL}"
            EXPECTED_HASH SHA256=e15dda82fe2fe8139dc2ac21a36d4ca01d5313c75f99f46c4e8a27709b7294bf
            STATUS DOWNLOAD_STATUS
            SHOW_PROGRESS
        )
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
            message(FATAL_ERROR "Failed to download OpenSSL: ${ERROR_MSG}")
        endif()
    endif()

    # Extract tarball
    if(NOT EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
        message(STATUS "  Extracting OpenSSL...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${OPENSSL_TARBALL}"
            WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract OpenSSL tarball")
        endif()
        # Move from openssl-3.4.0/ to src/openssl/
        file(RENAME "${OPENSSL_BUILD_DIR}/openssl-3.4.0" "${OPENSSL_SOURCE_DIR}")
    endif()

    # Configure OpenSSL
    message(STATUS "  Configuring OpenSSL for ${OPENSSL_TARGET}...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
            CC=${MUSL_GCC}
            REALGCC=${REAL_GCC}
            CFLAGS=${MUSL_KERNEL_CFLAGS}
            "${OPENSSL_SOURCE_DIR}/Configure"
            ${OPENSSL_TARGET}
            --prefix=${OPENSSL_PREFIX}
            no-shared
            no-tests
            no-ui-console
            -fPIC
        WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
        RESULT_VARIABLE CONFIG_RESULT
        OUTPUT_VARIABLE CONFIG_OUTPUT
        ERROR_VARIABLE CONFIG_ERROR
    )
    if(NOT CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to configure OpenSSL:\n${CONFIG_ERROR}")
    endif()

    message(STATUS "  Building OpenSSL (this takes a few minutes)...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env REALGCC=${REAL_GCC} make -j
        WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
        RESULT_VARIABLE BUILD_RESULT
        OUTPUT_VARIABLE BUILD_OUTPUT
        ERROR_VARIABLE BUILD_ERROR
    )
    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build OpenSSL:\n${BUILD_ERROR}")
    endif()

    # Install OpenSSL
    message(STATUS "  Installing OpenSSL...")
    execute_process(
        COMMAND make install_sw
        WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
        RESULT_VARIABLE INSTALL_RESULT
        OUTPUT_VARIABLE INSTALL_OUTPUT
        ERROR_VARIABLE INSTALL_ERROR
    )
    if(NOT INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to install OpenSSL:\n${INSTALL_ERROR}")
    endif()

    message(STATUS "  ${BoldGreen}OpenSSL${ColorReset} built and cached successfully")
else()
    message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} library found in cache: ${BoldMagenta}${OPENSSL_PREFIX}/lib64/libssl.a${ColorReset}")
endif()

# Create dummy target for dependency tracking (other ExternalProjects depend on this)
add_custom_target(openssl-musl DEPENDS libsodium-musl)

# Set OpenSSL variables for CMake find_package to use
# Note: OpenSSL 3.x installs to lib64/ by default on x86_64
set(OPENSSL_ROOT_DIR "${OPENSSL_PREFIX}" CACHE PATH "OpenSSL root directory" FORCE)
set(OPENSSL_INCLUDE_DIR "${OPENSSL_PREFIX}/include" CACHE PATH "OpenSSL include directory" FORCE)
set(OPENSSL_SSL_LIBRARY "${OPENSSL_PREFIX}/lib64/libssl.a" CACHE FILEPATH "OpenSSL SSL library" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_PREFIX}/lib64/libcrypto.a" CACHE FILEPATH "OpenSSL Crypto library" FORCE)

# Create imported targets that match what find_package(OpenSSL) would create
if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_PREFIX}/lib64/libcrypto.a"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
    )
endif()

if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
    set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_PREFIX}/lib64/libssl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
        INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
    )
endif()

# =============================================================================
# FFmpeg - Media encoding/decoding libraries
# =============================================================================
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
    set(FFMPEG_TARBALL "${FFMPEG_BUILD_DIR}/ffmpeg-7.1.tar.xz")
    if(NOT EXISTS "${FFMPEG_TARBALL}")
        message(STATUS "  Downloading FFmpeg 7.1...")
        file(DOWNLOAD
            "https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz"
            "${FFMPEG_TARBALL}"
            EXPECTED_HASH SHA256=40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6
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
        # Move from ffmpeg-7.1/ to src/ffmpeg/
        file(RENAME "${FFMPEG_BUILD_DIR}/ffmpeg-7.1" "${FFMPEG_SOURCE_DIR}")
    endif()

    # Configure FFmpeg with minimal build (only codecs we need)
    message(STATUS "  Configuring FFmpeg...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
            CC=${MUSL_GCC}
            REALGCC=${REAL_GCC}
            CFLAGS=${MUSL_KERNEL_CFLAGS}\ -fPIC
            "${FFMPEG_SOURCE_DIR}/configure"
            --prefix=${FFMPEG_PREFIX}
            --cc=${MUSL_GCC}
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
            --enable-protocol=file
            --enable-demuxer=mov,matroska,avi,gif,image2
            --enable-decoder=h264,hevc,vp8,vp9,av1,mpeg4,png,gif,mjpeg
            --enable-parser=h264,hevc,vp8,vp9,av1,mpeg4video
            --enable-swscale
            --enable-swresample
        WORKING_DIRECTORY "${FFMPEG_SOURCE_DIR}"
        RESULT_VARIABLE CONFIG_RESULT
        OUTPUT_VARIABLE CONFIG_OUTPUT
        ERROR_VARIABLE CONFIG_ERROR
    )
    if(NOT CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to configure FFmpeg:\n${CONFIG_ERROR}")
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

# Create dummy target for dependency tracking
add_custom_target(ffmpeg-musl DEPENDS openssl-musl)

# Set FFmpeg variables for use in the build
set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREFIX}/include" CACHE PATH "FFmpeg include directory" FORCE)
set(FFMPEG_LIBRARY_DIR "${FFMPEG_PREFIX}/lib" CACHE PATH "FFmpeg library directory" FORCE)

# Set individual library paths
set(FFMPEG_avformat_LIBRARY "${FFMPEG_PREFIX}/lib/libavformat.a" CACHE FILEPATH "FFmpeg avformat library" FORCE)
set(FFMPEG_avcodec_LIBRARY "${FFMPEG_PREFIX}/lib/libavcodec.a" CACHE FILEPATH "FFmpeg avcodec library" FORCE)
set(FFMPEG_avutil_LIBRARY "${FFMPEG_PREFIX}/lib/libavutil.a" CACHE FILEPATH "FFmpeg avutil library" FORCE)
set(FFMPEG_swscale_LIBRARY "${FFMPEG_PREFIX}/lib/libswscale.a" CACHE FILEPATH "FFmpeg swscale library" FORCE)
set(FFMPEG_swresample_LIBRARY "${FFMPEG_PREFIX}/lib/libswresample.a" CACHE FILEPATH "FFmpeg swresample library" FORCE)

# Set FFMPEG_LINK_LIBRARIES for use in target_link_libraries
set(FFMPEG_LINK_LIBRARIES
    "${FFMPEG_PREFIX}/lib/libavformat.a"
    "${FFMPEG_PREFIX}/lib/libavcodec.a"
    "${FFMPEG_PREFIX}/lib/libavutil.a"
    "${FFMPEG_PREFIX}/lib/libswscale.a"
    "${FFMPEG_PREFIX}/lib/libswresample.a"
    CACHE STRING "FFmpeg link libraries" FORCE
)

# =============================================================================
# SQLite3 - Embedded SQL database
# =============================================================================
message(STATUS "Configuring ${BoldBlue}SQLite3${ColorReset} from source...")

set(SQLITE3_PREFIX "${MUSL_DEPS_DIR_STATIC}/sqlite3")
set(SQLITE3_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/sqlite3-build")
set(SQLITE3_SOURCE_DIR "${SQLITE3_BUILD_DIR}/src/sqlite3")

# Build SQLite3 synchronously at configure time if not cached
if(NOT EXISTS "${SQLITE3_PREFIX}/lib/libsqlite3.a")
    message(STATUS "  SQLite3 library not found in cache, will build from source")
    message(STATUS "  This should be quick (SQLite is a single file)...")

    file(MAKE_DIRECTORY "${SQLITE3_BUILD_DIR}")
    file(MAKE_DIRECTORY "${SQLITE3_SOURCE_DIR}")

    # Download SQLite3 amalgamation (single-file distribution)
    set(SQLITE3_TARBALL "${SQLITE3_BUILD_DIR}/sqlite-amalgamation-3480000.zip")
    if(NOT EXISTS "${SQLITE3_TARBALL}")
        message(STATUS "  Downloading SQLite3 3.48.0...")
        file(DOWNLOAD
            "https://www.sqlite.org/2025/sqlite-amalgamation-3480000.zip"
            "${SQLITE3_TARBALL}"
            EXPECTED_HASH SHA256=d9a15a42db7c78f88fe3d3c5945acce2f4bfe9e4da9f685cd19f6ea1d40aa884
            STATUS DOWNLOAD_STATUS
            SHOW_PROGRESS
        )
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
            message(FATAL_ERROR "Failed to download SQLite3: ${ERROR_MSG}")
        endif()
    endif()

    # Extract archive
    if(NOT EXISTS "${SQLITE3_SOURCE_DIR}/sqlite3.c")
        message(STATUS "  Extracting SQLite3...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xf "${SQLITE3_TARBALL}"
            WORKING_DIRECTORY "${SQLITE3_BUILD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract SQLite3 archive")
        endif()
        # Move from sqlite-amalgamation-3480000/ to src/sqlite3/
        file(RENAME "${SQLITE3_BUILD_DIR}/sqlite-amalgamation-3480000" "${SQLITE3_SOURCE_DIR}")
    endif()

    # Compile SQLite3 to static library
    message(STATUS "  Building SQLite3...")
    file(MAKE_DIRECTORY "${SQLITE3_PREFIX}/lib")
    file(MAKE_DIRECTORY "${SQLITE3_PREFIX}/include")

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
            CC=${MUSL_GCC}
            REALGCC=${REAL_GCC}
            CFLAGS=${MUSL_KERNEL_CFLAGS}
            ${MUSL_GCC} -c
            -DSQLITE_THREADSAFE=1
            -DSQLITE_ENABLE_FTS5
            -DSQLITE_ENABLE_RTREE
            -DSQLITE_ENABLE_JSON1
            -O3
            -fPIC
            "${SQLITE3_SOURCE_DIR}/sqlite3.c"
            -o "${SQLITE3_BUILD_DIR}/sqlite3.o"
        RESULT_VARIABLE COMPILE_RESULT
        OUTPUT_VARIABLE COMPILE_OUTPUT
        ERROR_VARIABLE COMPILE_ERROR
    )
    if(NOT COMPILE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to compile SQLite3:\n${COMPILE_ERROR}")
    endif()

    # Create static library
    execute_process(
        COMMAND ${CMAKE_AR} rcs "${SQLITE3_PREFIX}/lib/libsqlite3.a" "${SQLITE3_BUILD_DIR}/sqlite3.o"
        RESULT_VARIABLE AR_RESULT
        OUTPUT_VARIABLE AR_OUTPUT
        ERROR_VARIABLE AR_ERROR
    )
    if(NOT AR_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to create SQLite3 static library:\n${AR_ERROR}")
    endif()

    # Copy headers
    file(COPY "${SQLITE3_SOURCE_DIR}/sqlite3.h" DESTINATION "${SQLITE3_PREFIX}/include")
    file(COPY "${SQLITE3_SOURCE_DIR}/sqlite3ext.h" DESTINATION "${SQLITE3_PREFIX}/include")

    message(STATUS "  ${BoldGreen}SQLite3${ColorReset} built and cached successfully")
else()
    message(STATUS "  ${BoldBlue}SQLite3${ColorReset} library found in cache: ${BoldMagenta}${SQLITE3_PREFIX}/lib/libsqlite3.a${ColorReset}")
endif()

# Create dummy target for dependency tracking
add_custom_target(sqlite3-musl DEPENDS zstd-musl)

# Set SQLite3 variables for use in the build
set(SQLITE3_INCLUDE_DIRS "${SQLITE3_PREFIX}/include" CACHE PATH "SQLite3 include directory" FORCE)
set(SQLITE3_LIBRARIES "${SQLITE3_PREFIX}/lib/libsqlite3.a" CACHE FILEPATH "SQLite3 library" FORCE)

# =============================================================================
# ALSA - Advanced Linux Sound Architecture
# =============================================================================
message(STATUS "Configuring ${BoldBlue}ALSA${ColorReset} from source...")

set(ALSA_PREFIX "${MUSL_DEPS_DIR_STATIC}/alsa-lib")
set(ALSA_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/alsa-lib-build")

include(ExternalProject)

# Only add external project if library doesn't exist
if(NOT EXISTS "${ALSA_PREFIX}/lib/libasound.a")
    message(STATUS "  ALSA library not found in cache, will build from source")
    ExternalProject_Add(alsa-lib-musl
        URL https://www.alsa-project.org/files/pub/lib/alsa-lib-1.2.12.tar.bz2
        URL_HASH SHA256=4868cd908627279da5a634f468701625be8cc251d84262c7e5b6a218391ad0d2
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${ALSA_BUILD_DIR}
        STAMP_DIR ${ALSA_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=${MUSL_KERNEL_CFLAGS} <SOURCE_DIR>/configure --host=x86_64-linux-gnu --prefix=${ALSA_PREFIX} --enable-static --disable-shared --disable-maintainer-mode
        BUILD_COMMAND env REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${ALSA_PREFIX}/lib/libasound.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}ALSA${ColorReset} library found in cache: ${BoldMagenta}${ALSA_PREFIX}/lib/libasound.a${ColorReset}")
    # Create a dummy target so dependencies can reference it
    add_custom_target(alsa-lib-musl)
endif()

# Set ALSA variables for PortAudio to find
set(ALSA_LIBRARIES "${ALSA_PREFIX}/lib/libasound.a")
set(ALSA_INCLUDE_DIRS "${ALSA_PREFIX}/include")

# =============================================================================
# PortAudio - Audio I/O library
# =============================================================================
message(STATUS "Configuring ${BoldBlue}PortAudio${ColorReset} from source...")

set(PORTAUDIO_PREFIX "${MUSL_DEPS_DIR_STATIC}/portaudio")
set(PORTAUDIO_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/portaudio-build")

# Only add external project if library doesn't exist
if(NOT EXISTS "${PORTAUDIO_PREFIX}/lib/libportaudio.a")
    message(STATUS "  PortAudio library not found in cache, will build from source")
    ExternalProject_Add(portaudio-musl
        URL http://files.portaudio.com/archives/pa_stable_v190700_20210406.tgz
        URL_HASH SHA256=47efbf42c77c19a05d22e627d42873e991ec0c1357219c0d74ce6a2948cb2def
        TLS_VERIFY FALSE  # PortAudio's SSL cert is expired (Dec 2025)
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${PORTAUDIO_BUILD_DIR}
        STAMP_DIR ${PORTAUDIO_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC PKG_CONFIG_PATH=${ALSA_PREFIX}/lib/pkgconfig <SOURCE_DIR>/configure --prefix=${PORTAUDIO_PREFIX} --enable-static --disable-shared --with-alsa --without-jack --without-oss
        BUILD_COMMAND env REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${PORTAUDIO_PREFIX}/lib/libportaudio.a
        DEPENDS alsa-lib-musl
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}PortAudio${ColorReset} library found in cache: ${BoldMagenta}${PORTAUDIO_PREFIX}/lib/libportaudio.a${ColorReset}")
    # Create a dummy target so dependencies can reference it
    add_custom_target(portaudio-musl DEPENDS alsa-lib-musl)
endif()

set(PORTAUDIO_LIBRARIES "${PORTAUDIO_PREFIX}/lib/libportaudio.a")
set(PORTAUDIO_INCLUDE_DIRS "${PORTAUDIO_PREFIX}/include")

# =============================================================================
# libopus - Audio codec library
# =============================================================================
message(STATUS "Configuring ${BoldBlue}libopus${ColorReset} from source...")

set(OPUS_PREFIX "${MUSL_DEPS_DIR_STATIC}/opus")
set(OPUS_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/opus-build")

# Only add external project if library doesn't exist
if(NOT EXISTS "${OPUS_PREFIX}/lib/libopus.a")
    message(STATUS "  libopus library not found in cache, will build from source")
    ExternalProject_Add(opus-musl
        URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
        URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${OPUS_BUILD_DIR}
        STAMP_DIR ${OPUS_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC <SOURCE_DIR>/configure --prefix=${OPUS_PREFIX} --enable-static --disable-shared --disable-doc --disable-extra-programs
        BUILD_COMMAND env REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${OPUS_PREFIX}/lib/libopus.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}libopus${ColorReset} library found in cache: ${BoldMagenta}${OPUS_PREFIX}/lib/libopus.a${ColorReset}")
    # Create a dummy target so dependencies can reference it
    add_custom_target(opus-musl)
endif()

set(OPUS_LIBRARIES "${OPUS_PREFIX}/lib/libopus.a")
set(OPUS_INCLUDE_DIRS "${OPUS_PREFIX}/include")

# =============================================================================
# libexecinfo - Backtrace support for musl
# =============================================================================
message(STATUS "Configuring ${BoldBlue}libexecinfo${ColorReset} from source...")

set(LIBEXECINFO_PREFIX "${MUSL_DEPS_DIR_STATIC}/libexecinfo")
set(LIBEXECINFO_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libexecinfo-build")

# Only add external project if library doesn't exist
if(NOT EXISTS "${LIBEXECINFO_PREFIX}/lib/libexecinfo.a")
    message(STATUS "  libexecinfo library not found in cache, will build from source")
    ExternalProject_Add(libexecinfo-musl
        GIT_REPOSITORY https://github.com/mikroskeem/libexecinfo.git
        GIT_TAG master
        PREFIX ${LIBEXECINFO_BUILD_DIR}
        STAMP_DIR ${LIBEXECINFO_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND ""
        BUILD_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j -C <SOURCE_DIR>
        INSTALL_COMMAND env CC=${MUSL_GCC} make -C <SOURCE_DIR> install PREFIX=${LIBEXECINFO_PREFIX}
        BUILD_IN_SOURCE 1
        BUILD_BYPRODUCTS ${LIBEXECINFO_PREFIX}/lib/libexecinfo.a
        LOG_DOWNLOAD TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}libexecinfo${ColorReset} library found in cache: ${BoldMagenta}${LIBEXECINFO_PREFIX}/lib/libexecinfo.a${ColorReset}")
    # Create a dummy target so dependencies can reference it
    add_custom_target(libexecinfo-musl)
endif()

set(LIBEXECINFO_LIBRARIES "${LIBEXECINFO_PREFIX}/lib/libexecinfo.a")
set(LIBEXECINFO_INCLUDE_DIRS "${LIBEXECINFO_PREFIX}/include")

# =============================================================================
# BearSSL - TLS library for SSH key fetching
# =============================================================================
message(STATUS "Configuring ${BoldBlue}BearSSL${ColorReset} from source...")

# BearSSL doesn't use CMake, so we build it manually
set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/bearssl")
set(BEARSSL_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/bearssl-build")
set(BEARSSL_LIB "${BEARSSL_BUILD_DIR}/libbearssl.a")

if(EXISTS "${BEARSSL_SOURCE_DIR}")
    if(NOT EXISTS "${BEARSSL_LIB}")
        message(STATUS "Building BearSSL library (static only)...")
        file(MAKE_DIRECTORY "${BEARSSL_BUILD_DIR}")

        # Clean any previous build to avoid leftover targets
        execute_process(
            COMMAND make clean
            WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
            OUTPUT_QUIET
            ERROR_QUIET
        )

        # Build the static library target with parallel jobs for faster builds
        # For musl: disable getentropy() (not in musl), force /dev/urandom, disable fortification
        # Output is captured and only shown on failure
        execute_process(
            COMMAND make -j lib CC=${MUSL_GCC} AR=${CMAKE_AR} CFLAGS=-DBR_USE_GETENTROPY=0\ -DBR_USE_URANDOM=1\ -U_FORTIFY_SOURCE\ -D_FORTIFY_SOURCE=0\ -fno-stack-protector\ -fPIC
            WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
            RESULT_VARIABLE BEARSSL_MAKE_RESULT
            OUTPUT_VARIABLE BEARSSL_MAKE_OUTPUT
            ERROR_VARIABLE BEARSSL_MAKE_ERROR
            OUTPUT_QUIET
        )

        if(BEARSSL_MAKE_RESULT EQUAL 0)
            # Copy library to cache
            file(COPY "${BEARSSL_SOURCE_DIR}/build/libbearssl.a"
                 DESTINATION "${BEARSSL_BUILD_DIR}")
            message(STATUS "  ${BoldBlue}BearSSL${ColorReset} library built and cached successfully")
        else()
            message(FATAL_ERROR "BearSSL build failed with exit code ${BEARSSL_MAKE_RESULT}\nOutput: ${BEARSSL_MAKE_OUTPUT}\nError: ${BEARSSL_MAKE_ERROR}")
        endif()
    else()
        message(STATUS "Using cached ${BoldBlue}BearSSL${ColorReset} library: ${BoldMagenta}${BEARSSL_LIB}${ColorReset}")
    endif()

    # Create an imported library target that matches what BearSSL.cmake creates
    add_library(bearssl_static STATIC IMPORTED GLOBAL)
    set_target_properties(bearssl_static PROPERTIES
        IMPORTED_LOCATION "${BEARSSL_LIB}"
    )
    target_include_directories(bearssl_static INTERFACE
        "${BEARSSL_SOURCE_DIR}/inc"
    )

    set(BEARSSL_LIBRARIES bearssl_static)
    set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SOURCE_DIR}/inc")
else()
    message(WARNING "BearSSL submodule not found - GitHub/GitLab key fetching will be disabled")
    set(BEARSSL_LIBRARIES "")
    set(BEARSSL_INCLUDE_DIRS "")
endif()

# =============================================================================
# PCRE2 - Perl Compatible Regular Expressions
# =============================================================================
message(STATUS "Configuring ${BoldBlue}PCRE2${ColorReset} from source...")

set(PCRE2_PREFIX "${MUSL_DEPS_DIR_STATIC}/pcre2")
set(PCRE2_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/pcre2-build")

# Only add external project if library doesn't exist
if(NOT EXISTS "${PCRE2_PREFIX}/lib/libpcre2-8.a")
    message(STATUS "  PCRE2 library not found in cache, will build from source")
    ExternalProject_Add(pcre2-musl
        URL https://github.com/PCRE2Project/pcre2/archive/refs/tags/pcre2-10.47.tar.gz
        URL_HASH SHA256=409c443549b13b216da40049850a32f3e6c57d4224ab11553ab5a786878a158e
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${PCRE2_BUILD_DIR}
        STAMP_DIR ${PCRE2_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC <SOURCE_DIR>/configure --host=x86_64-linux-gnu --prefix=${PCRE2_PREFIX} --enable-static --disable-shared --enable-pcre2-8 --disable-pcre2-16 --disable-pcre2-32 --disable-maintainer-mode
        BUILD_COMMAND env REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${PCRE2_PREFIX}/lib/libpcre2-8.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}PCRE2${ColorReset} library found in cache: ${BoldMagenta}${PCRE2_PREFIX}/lib/libpcre2-8.a${ColorReset}")
    # Create a dummy target so dependencies can reference it
    add_custom_target(pcre2-musl)
endif()

set(PCRE2_LIBRARIES "${PCRE2_PREFIX}/lib/libpcre2-8.a")
set(PCRE2_INCLUDE_DIRS "${PCRE2_PREFIX}/include")

# =============================================================================
# systemd - System daemon library for hardware discovery and session management
# =============================================================================
# systemd is intentionally skipped for musl builds. The code explicitly excludes
# systemd linking for musl (lib/CMakeLists.txt: if(NOT USE_MUSL)). The keepawake
# feature uses a glibc-independent fallback for musl. Attempting to build systemd
# v259.1 for musl fails due to extensive glibc/musl incompatibilities and is
# unnecessary since the built library is never linked.
message(STATUS "Skipping ${BoldBlue}systemd${ColorReset} for musl build - keepawake uses glibc-independent fallback")

# Create dummy target for any cmake code that might check for it
add_custom_target(systemd-musl)

# =============================================================================
# libwebsockets - WebSocket transport for browser clients
# =============================================================================
message(STATUS "Configuring ${BoldBlue}libwebsockets${ColorReset} from source...")

set(LWS_PREFIX "${MUSL_DEPS_DIR_STATIC}/libwebsockets")
set(LWS_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libwebsockets-build")

if(NOT EXISTS "${LWS_PREFIX}/lib/libwebsockets.a")
    message(STATUS "  libwebsockets library not found in cache, will build from source")

    # Pass musl-gcc path to the toolchain file via cache variable
    set(MUSL_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchains/MuslGcc.cmake")

    ExternalProject_Add(libwebsockets-musl
        URL https://github.com/warmcat/libwebsockets/archive/refs/tags/v4.5.2.tar.gz
        URL_HASH SHA256=04244efb7a6438c8c6bfc79b21214db5950f72c9cf57e980af57ca321aae87b2
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${LWS_BUILD_DIR}
        STAMP_DIR ${LWS_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        DEPENDS zlib-musl
        CMAKE_ARGS
            -DCMAKE_TOOLCHAIN_FILE=${MUSL_TOOLCHAIN_FILE}
            -DMUSL_GCC_PATH=${MUSL_GCC}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${LWS_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_C_FLAGS=-O2\ -fPIC\ -Wno-sign-conversion\ -Wno-error\ -isystem\ ${KERNEL_HEADERS_DIR}
            -DLWS_WITH_SHARED=OFF
            -DLWS_WITH_STATIC=ON
            -DLWS_WITHOUT_TESTAPPS=ON
            -DLWS_WITHOUT_TEST_SERVER=ON
            -DLWS_WITHOUT_TEST_SERVER_EXTPOLL=ON
            -DLWS_WITHOUT_TEST_PING=ON
            -DLWS_WITHOUT_TEST_CLIENT=ON
            -DLWS_WITH_SSL=OFF
            -DLWS_WITH_LIBEV=OFF
            -DLWS_WITH_LIBUV=OFF
            -DLWS_WITH_LIBEVENT=OFF
            -DLWS_WITH_GLIB=OFF
            -DLWS_WITH_SYSTEMD=OFF
            -DLWS_WITH_LIBCAP=OFF
            -DLWS_WITH_JOSE=OFF
            -DLWS_WITH_GENCRYPTO=OFF
            -DLWS_IPV6=ON
            -DLWS_UNIX_SOCK=ON
            -DLWS_WITHOUT_DAEMONIZE=ON
            -DLWS_WITHOUT_EXTENSIONS=OFF
            -DLWS_WITH_ZLIB=ON
            -DLWS_WITH_BUNDLED_ZLIB=ON
            -DZLIB_INCLUDE_DIR=${ZLIB_INCLUDE_DIR}
            -DZLIB_LIBRARY=${ZLIB_LIBRARY}
            -DLWS_WITH_SOCKS5=OFF
        BUILD_BYPRODUCTS ${LWS_PREFIX}/lib/libwebsockets.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}libwebsockets${ColorReset} library found in cache: ${BoldMagenta}${LWS_PREFIX}/lib/libwebsockets.a${ColorReset}")
    add_custom_target(libwebsockets-musl)
endif()

set(LIBWEBSOCKETS_LIBRARIES "${LWS_PREFIX}/lib/libwebsockets.a")
set(LIBWEBSOCKETS_INCLUDE_DIRS "${LWS_PREFIX}/include")
set(LIBWEBSOCKETS_BUILD_TARGET libwebsockets-musl)
add_compile_definitions(HAVE_LIBWEBSOCKETS=1)

# Create placeholder directories so CMake validation doesn't fail at configure time
file(MAKE_DIRECTORY "${LIBWEBSOCKETS_INCLUDE_DIRS}")

# Create imported target for libwebsockets (musl build) to match Libwebsockets.cmake behavior
add_library(websockets STATIC IMPORTED GLOBAL)
set_target_properties(websockets PROPERTIES
    IMPORTED_LOCATION "${LIBWEBSOCKETS_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBWEBSOCKETS_INCLUDE_DIRS}"
)
add_dependencies(websockets libwebsockets-musl)

# =============================================================================
# Abseil-cpp - Google's C++ library for WebRTC dependencies
# =============================================================================
message(STATUS "Configuring ${BoldBlue}Abseil-cpp${ColorReset} from source...")

set(ABSEIL_PREFIX "${MUSL_DEPS_DIR_STATIC}/abseil")
set(ABSEIL_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/abseil-build")

# Only add external project if library doesn't exist
if(NOT EXISTS "${ABSEIL_PREFIX}/lib/libabsl_base.a")
    message(STATUS "  Abseil library not found in cache, will build from source")
    ExternalProject_Add(abseil-musl
        URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20250814.1.tar.gz
        URL_HASH SHA256=1692f77d1739bacf3f94337188b78583cf09bab7e420d2dc6c5605a4f86785a1
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${ABSEIL_BUILD_DIR}
        STAMP_DIR ${ABSEIL_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CMAKE_ARGS
            -DCMAKE_C_COMPILER=${MUSL_GCC}
            -DCMAKE_CXX_COMPILER=clang++
            -DCMAKE_CXX_STANDARD=17
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${ABSEIL_PREFIX}
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_C_FLAGS=-O3\ -fPIC
            -DCMAKE_CXX_FLAGS=-O3\ -fPIC\ -target\ x86_64-linux-musl\ -stdlib=libc++
        BUILD_BYPRODUCTS
            ${ABSEIL_PREFIX}/lib/libabsl_base.a
            ${ABSEIL_PREFIX}/lib/libabsl_strings.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}Abseil${ColorReset} library found in cache: ${BoldMagenta}${ABSEIL_PREFIX}/lib/libabsl_base.a${ColorReset}")
    # Create a dummy target so dependencies can reference it
    add_custom_target(abseil-musl)
endif()

set(ABSEIL_LIBRARIES "${ABSEIL_PREFIX}/lib")
set(ABSEIL_INCLUDE_DIRS "${ABSEIL_PREFIX}/include")

# =============================================================================
# yyjson - Fast JSON library (writer-only for structured logging)
# =============================================================================
# yyjson is a high-performance JSON library. We use only the writer API
# for structured log output. YYJSON_DISABLE_READER is enabled to reduce
# binary size (reader code is not needed).
message(STATUS "Configuring ${BoldBlue}yyjson${ColorReset} from source...")

set(YYJSON_PREFIX "${MUSL_DEPS_DIR_STATIC}/yyjson")
set(YYJSON_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/yyjson-build")

if(NOT EXISTS "${YYJSON_PREFIX}/lib/libyyjson.a")
    message(STATUS "  yyjson library not found in cache, will build from source")
    ExternalProject_Add(yyjson-musl
        URL https://github.com/ibireme/yyjson/archive/refs/tags/0.12.0.tar.gz
        URL_HASH SHA256=b16246f617b2a136c78d73e5e2647c6f1de1313e46678062985bdcf1f40bb75d
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        TLS_VERIFY FALSE
        PREFIX ${YYJSON_BUILD_DIR}
        STAMP_DIR ${YYJSON_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CMAKE_ARGS
            -DCMAKE_C_COMPILER=${MUSL_GCC}
            -DCMAKE_INSTALL_PREFIX=${YYJSON_PREFIX}
            -DCMAKE_BUILD_TYPE=Release
            -DYYJSON_DISABLE_READER=ON
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_C_FLAGS=-O3\ -fPIC
        BUILD_BYPRODUCTS ${YYJSON_PREFIX}/lib/libyyjson.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}yyjson${ColorReset} library found in cache: ${BoldMagenta}${YYJSON_PREFIX}/lib/libyyjson.a${ColorReset}")
    # Create a dummy target so dependencies can reference it
    add_custom_target(yyjson-musl)
endif()

set(YYJSON_LIBRARIES "${YYJSON_PREFIX}/lib/libyyjson.a")
set(YYJSON_INCLUDE_DIRS "${YYJSON_PREFIX}/include")

# =============================================================================
# libffi - Function Interface library for glib gobject (built synchronously)
# =============================================================================
message(STATUS "Configuring ${BoldBlue}libffi${ColorReset} from source...")

set(LIBFFI_PREFIX "${MUSL_DEPS_DIR_STATIC}/libffi")

if(NOT EXISTS "${LIBFFI_PREFIX}/lib/libffi.a")
    message(STATUS "  libffi library not found in cache, will build from source")

    set(LIBFFI_DOWNLOAD_DIR "${MUSL_DEPS_DIR_STATIC}/libffi-src")
    set(LIBFFI_TARBALL "${LIBFFI_DOWNLOAD_DIR}/libffi-3.4.4.tar.gz")
    set(LIBFFI_SOURCE_DIR "${LIBFFI_DOWNLOAD_DIR}/libffi-3.4.4")

    file(MAKE_DIRECTORY "${LIBFFI_DOWNLOAD_DIR}")

    if(NOT EXISTS "${LIBFFI_SOURCE_DIR}")
        if(NOT EXISTS "${LIBFFI_TARBALL}")
            message(STATUS "    Downloading libffi...")
            file(DOWNLOAD
                "https://github.com/libffi/libffi/releases/download/v3.4.4/libffi-3.4.4.tar.gz"
                "${LIBFFI_TARBALL}"
                EXPECTED_HASH SHA256=d66c56ad259a82cf2a9dfc408b32bf5da52371500b84745f7fb8b645712df676
                SHOW_PROGRESS
            )
        endif()

        message(STATUS "    Extracting libffi...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${LIBFFI_TARBALL}"
            WORKING_DIRECTORY "${LIBFFI_DOWNLOAD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract libffi")
        endif()
    endif()

    set(LIBFFI_BUILD_DIR "${LIBFFI_DOWNLOAD_DIR}/build")
    file(MAKE_DIRECTORY "${LIBFFI_BUILD_DIR}")

    message(STATUS "    Configuring libffi...")
    set(ENV{CC} ${MUSL_GCC})
    set(ENV{CFLAGS} "-O2 -fPIC")
    execute_process(
        COMMAND "${LIBFFI_SOURCE_DIR}/configure"
            --prefix=${LIBFFI_PREFIX}
            --enable-static
            --disable-shared
            --disable-exec-static-tramp
        WORKING_DIRECTORY "${LIBFFI_BUILD_DIR}"
        RESULT_VARIABLE LIBFFI_CONFIG_RESULT
        ERROR_VARIABLE LIBFFI_CONFIG_ERROR
    )
    if(NOT LIBFFI_CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "libffi configure failed:\n${LIBFFI_CONFIG_ERROR}")
    endif()

    message(STATUS "    Building libffi...")
    execute_process(
        COMMAND make -j
        WORKING_DIRECTORY "${LIBFFI_BUILD_DIR}"
        RESULT_VARIABLE LIBFFI_BUILD_RESULT
        ERROR_VARIABLE LIBFFI_BUILD_ERROR
    )
    if(NOT LIBFFI_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "libffi build failed:\n${LIBFFI_BUILD_ERROR}")
    endif()

    message(STATUS "    Installing libffi...")
    execute_process(
        COMMAND make install
        WORKING_DIRECTORY "${LIBFFI_BUILD_DIR}"
        RESULT_VARIABLE LIBFFI_INSTALL_RESULT
        ERROR_VARIABLE LIBFFI_INSTALL_ERROR
    )
    if(NOT LIBFFI_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "libffi install failed:\n${LIBFFI_INSTALL_ERROR}")
    endif()

    message(STATUS "    ${BoldGreen}libffi${ColorReset} built successfully")
else()
    message(STATUS "  ${BoldBlue}libffi${ColorReset} library found in cache: ${BoldMagenta}${LIBFFI_PREFIX}/lib/libffi.a${ColorReset}")
endif()

set(LIBFFI_LIBRARIES "${LIBFFI_PREFIX}/lib/libffi.a")
set(LIBFFI_INCLUDE_DIRS "${LIBFFI_PREFIX}/include")
file(MAKE_DIRECTORY "${LIBFFI_PREFIX}/include" "${LIBFFI_PREFIX}/lib")

# =============================================================================
# GTK4 and dependencies - Static build for musl
# =============================================================================
# Build GTK4 ecosystem from source for musl static linking
# Required for Ghostty: gtk4, graphene-gobject-1.0, libadwaita-1
message(STATUS "Configuring ${BoldBlue}GTK4 ecosystem${ColorReset} from source...")

# Define glib prefix first (needed before glib build)
set(GLIB_PREFIX "${MUSL_DEPS_DIR_STATIC}/glib")
set(GLIB_LIBRARIES "${GLIB_PREFIX}/lib/libglib-2.0.a")
set(GLIB_INCLUDE_DIRS "${GLIB_PREFIX}/include/glib-2.0")

set(GTK4_PREFIX "${MUSL_DEPS_DIR_STATIC}/gtk4")
set(GTK4_LIBRARIES "${GTK4_PREFIX}/lib/libgtk-4.a")
set(GTK4_INCLUDE_DIRS "${GTK4_PREFIX}/include/gtk-4.0")

set(GRAPHENE_PREFIX "${MUSL_DEPS_DIR_STATIC}/graphene")
set(GRAPHENE_LIBRARIES "${GRAPHENE_PREFIX}/lib/libgraphene-1.a")
set(GRAPHENE_INCLUDE_DIRS "${GRAPHENE_PREFIX}/include/graphene-1.0")

set(LIBADWAITA_PREFIX "${MUSL_DEPS_DIR_STATIC}/libadwaita")
set(LIBADWAITA_LIBRARIES "${LIBADWAITA_PREFIX}/lib/libadwaita-1.a")
set(LIBADWAITA_INCLUDE_DIRS "${LIBADWAITA_PREFIX}/include/adwaita-1")

# Create placeholder directories
file(MAKE_DIRECTORY "${GTK4_PREFIX}/include" "${GTK4_PREFIX}/lib")
file(MAKE_DIRECTORY "${GRAPHENE_PREFIX}/include" "${GRAPHENE_PREFIX}/lib")
file(MAKE_DIRECTORY "${LIBADWAITA_PREFIX}/include" "${LIBADWAITA_PREFIX}/lib")

# =============================================================================
# wayland-protocols - Wayland protocol definitions (header-only)
# =============================================================================
message(STATUS "Configuring ${BoldBlue}wayland-protocols${ColorReset} from source...")

set(WAYLAND_PROTOCOLS_PREFIX "${MUSL_DEPS_DIR_STATIC}/wayland-protocols")

if(NOT EXISTS "${WAYLAND_PROTOCOLS_PREFIX}/share/pkgconfig/wayland-protocols.pc")
    message(STATUS "  wayland-protocols not found, will build from source")

    set(WP_SOURCE_DIR "${MUSL_DEPS_DIR_STATIC}/wayland-protocols-src")

    if(NOT EXISTS "${WP_SOURCE_DIR}")
        message(STATUS "    Cloning wayland-protocols...")
        execute_process(
            COMMAND git clone --depth 1 --branch 1.32 https://gitlab.freedesktop.org/wayland/wayland-protocols.git "${WP_SOURCE_DIR}"
            RESULT_VARIABLE CLONE_RESULT
        )
        if(NOT CLONE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to clone wayland-protocols")
        endif()
    endif()

    set(WP_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/wayland-protocols-build")
    file(MAKE_DIRECTORY "${WP_BUILD_DIR}")

    message(STATUS "    Configuring wayland-protocols with meson...")
    execute_process(
        COMMAND env CC=${MUSL_GCC} CXX=clang++ ${MESON_EXECUTABLE} setup
            "${WP_BUILD_DIR}"
            "${WP_SOURCE_DIR}"
            --prefix=${WAYLAND_PROTOCOLS_PREFIX}
            --buildtype=release
            -Dtests=false
        RESULT_VARIABLE WP_CONFIG_RESULT
        ERROR_VARIABLE WP_CONFIG_ERROR
    )
    if(NOT WP_CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "wayland-protocols meson setup failed:\n${WP_CONFIG_ERROR}")
    endif()

    message(STATUS "    Installing wayland-protocols...")
    execute_process(
        COMMAND ${MESON_EXECUTABLE} install -C "${WP_BUILD_DIR}"
        RESULT_VARIABLE WP_INSTALL_RESULT
        ERROR_VARIABLE WP_INSTALL_ERROR
    )
    if(NOT WP_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "wayland-protocols install failed:\n${WP_INSTALL_ERROR}")
    endif()

    message(STATUS "    ${BoldGreen}wayland-protocols${ColorReset} built successfully")
else()
    message(STATUS "  ${BoldBlue}wayland-protocols${ColorReset} found in cache: ${BoldMagenta}${WAYLAND_PROTOCOLS_PREFIX}/share/pkgconfig/wayland-protocols.pc${ColorReset}")
endif()

# =============================================================================
# libxkbcommon - Keyboard handling library for wayland
# =============================================================================
message(STATUS "Configuring ${BoldBlue}libxkbcommon${ColorReset} from source...")

set(XKBCOMMON_PREFIX "${MUSL_DEPS_DIR_STATIC}/xkbcommon")

if(NOT EXISTS "${XKBCOMMON_PREFIX}/lib/libxkbcommon.a")
    message(STATUS "  libxkbcommon not found, will build from source")

    set(XKB_DOWNLOAD_DIR "${MUSL_DEPS_DIR_STATIC}/xkbcommon-src")
    set(XKB_TARBALL "${XKB_DOWNLOAD_DIR}/xkbcommon-1.5.0.tar.xz")
    set(XKB_SOURCE_DIR "${XKB_DOWNLOAD_DIR}/libxkbcommon-1.5.0")

    file(MAKE_DIRECTORY "${XKB_DOWNLOAD_DIR}")

    if(NOT EXISTS "${XKB_SOURCE_DIR}")
        if(NOT EXISTS "${XKB_TARBALL}")
            message(STATUS "    Downloading libxkbcommon...")
            file(DOWNLOAD
                "https://xkbcommon.org/download/libxkbcommon-1.5.0.tar.xz"
                "${XKB_TARBALL}"
                EXPECTED_HASH SHA256=560f11c4bbbca10f495f3ef7d3a6aa4ca62b4f8fb0b52e7d459d18a26e46e017
                SHOW_PROGRESS
            )
        endif()

        message(STATUS "    Extracting libxkbcommon...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xJf "${XKB_TARBALL}"
            WORKING_DIRECTORY "${XKB_DOWNLOAD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract libxkbcommon")
        endif()
    endif()

    set(XKB_BUILD_DIR "${XKB_DOWNLOAD_DIR}/build")
    file(MAKE_DIRECTORY "${XKB_BUILD_DIR}")

    message(STATUS "    Configuring libxkbcommon with meson...")
    execute_process(
        COMMAND env CC=${MUSL_GCC} CXX=clang++ PKG_CONFIG_LIBDIR=${XKBCOMMON_PREFIX}/lib/pkgconfig PKG_CONFIG_PATH= ${MESON_EXECUTABLE} setup
            "${XKB_BUILD_DIR}"
            "${XKB_SOURCE_DIR}"
            --prefix=${XKBCOMMON_PREFIX}
            --buildtype=release
            --default-library=static
            -Denable-x11=false
            -Denable-tools=false
            -Denable-xkbregistry=false
        RESULT_VARIABLE XKB_CONFIG_RESULT
        ERROR_VARIABLE XKB_CONFIG_ERROR
    )
    if(NOT XKB_CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "libxkbcommon meson setup failed:\n${XKB_CONFIG_ERROR}")
    endif()

    message(STATUS "    Building libxkbcommon...")
    execute_process(
        COMMAND ${MESON_EXECUTABLE} compile -C "${XKB_BUILD_DIR}"
        RESULT_VARIABLE XKB_BUILD_RESULT
        ERROR_VARIABLE XKB_BUILD_ERROR
    )
    if(NOT XKB_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "libxkbcommon build failed:\n${XKB_BUILD_ERROR}")
    endif()

    message(STATUS "    Installing libxkbcommon...")
    execute_process(
        COMMAND ${MESON_EXECUTABLE} install -C "${XKB_BUILD_DIR}"
        RESULT_VARIABLE XKB_INSTALL_RESULT
        ERROR_VARIABLE XKB_INSTALL_ERROR
    )
    if(NOT XKB_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "libxkbcommon install failed:\n${XKB_INSTALL_ERROR}")
    endif()

    message(STATUS "    ${BoldGreen}libxkbcommon${ColorReset} built successfully")
else()
    message(STATUS "  ${BoldBlue}libxkbcommon${ColorReset} found in cache: ${BoldMagenta}${XKBCOMMON_PREFIX}/lib/libxkbcommon.a${ColorReset}")
endif()

set(XKBCOMMON_LIBRARIES "${XKBCOMMON_PREFIX}/lib/libxkbcommon.a")
set(XKBCOMMON_INCLUDE_DIRS "${XKBCOMMON_PREFIX}/include")
file(MAKE_DIRECTORY "${XKBCOMMON_PREFIX}/include" "${XKBCOMMON_PREFIX}/lib")

# =============================================================================
# libxml2 - XML parser library (required by wayland)
# =============================================================================
message(STATUS "Configuring ${BoldBlue}libxml2${ColorReset} from source...")

set(LIBXML2_PREFIX "${MUSL_DEPS_DIR_STATIC}/libxml2")

if(NOT EXISTS "${LIBXML2_PREFIX}/lib/libxml2.a")
    message(STATUS "  libxml2 not found, will build from source")

    set(XML2_DOWNLOAD_DIR "${MUSL_DEPS_DIR_STATIC}/libxml2-src")
    set(XML2_TARBALL "${XML2_DOWNLOAD_DIR}/libxml2-2.13.0.tar.xz")
    set(XML2_SOURCE_DIR "${XML2_DOWNLOAD_DIR}/libxml2-2.13.0")

    file(MAKE_DIRECTORY "${XML2_DOWNLOAD_DIR}")

    if(NOT EXISTS "${XML2_SOURCE_DIR}")
        if(NOT EXISTS "${XML2_TARBALL}")
            message(STATUS "    Downloading libxml2...")
            file(DOWNLOAD
                "https://download.gnome.org/sources/libxml2/2.13/libxml2-2.13.0.tar.xz"
                "${XML2_TARBALL}"
                EXPECTED_HASH SHA256=d5a2f36bea96e1fb8297c6046fb02016c152d81ed58e65f3d20477de85291bc9
                SHOW_PROGRESS
            )
        endif()

        message(STATUS "    Extracting libxml2...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xJf "${XML2_TARBALL}"
            WORKING_DIRECTORY "${XML2_DOWNLOAD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract libxml2")
        endif()
    endif()

    message(STATUS "    Configuring libxml2...")
    execute_process(
        COMMAND env CFLAGS=-Os ${XML2_SOURCE_DIR}/configure
            --prefix=${LIBXML2_PREFIX}
            --host=x86_64-linux-musl
            CC=${MUSL_GCC}
            --disable-shared
            --enable-static
            --without-python
            --without-perl
            --without-iconv
            --with-zlib=${ZLIB_PREFIX}
        WORKING_DIRECTORY ${XML2_SOURCE_DIR}
        RESULT_VARIABLE XML2_CONFIG_RESULT
        ERROR_VARIABLE XML2_CONFIG_ERROR
    )
    if(NOT XML2_CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "libxml2 configure failed:\n${XML2_CONFIG_ERROR}")
    endif()

    message(STATUS "    Building libxml2...")
    execute_process(
        COMMAND make -j4
        WORKING_DIRECTORY ${XML2_SOURCE_DIR}
        RESULT_VARIABLE XML2_BUILD_RESULT
        ERROR_VARIABLE XML2_BUILD_ERROR
    )
    if(NOT XML2_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "libxml2 build failed:\n${XML2_BUILD_ERROR}")
    endif()

    message(STATUS "    Installing libxml2...")
    execute_process(
        COMMAND make install
        WORKING_DIRECTORY ${XML2_SOURCE_DIR}
        RESULT_VARIABLE XML2_INSTALL_RESULT
        ERROR_VARIABLE XML2_INSTALL_ERROR
    )
    if(NOT XML2_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "libxml2 install failed:\n${XML2_INSTALL_ERROR}")
    endif()

    message(STATUS "    ${BoldGreen}libxml2${ColorReset} built successfully")
else()
    message(STATUS "  ${BoldBlue}libxml2${ColorReset} found in cache: ${BoldMagenta}${LIBXML2_PREFIX}/lib/libxml2.a${ColorReset}")
endif()

set(LIBXML2_LIBRARIES "${LIBXML2_PREFIX}/lib/libxml2.a")
set(LIBXML2_INCLUDE_DIRS "${LIBXML2_PREFIX}/include")
file(MAKE_DIRECTORY "${LIBXML2_PREFIX}/include" "${LIBXML2_PREFIX}/lib")

# =============================================================================
# expat - XML parser library (required by wayland)
# =============================================================================
message(STATUS "Configuring ${BoldBlue}expat${ColorReset} from source...")

set(EXPAT_PREFIX "${MUSL_DEPS_DIR_STATIC}/expat")

if(NOT EXISTS "${EXPAT_PREFIX}/lib/libexpat.a")
    message(STATUS "  expat library not found, will build from source")

    set(EXPAT_DOWNLOAD_DIR "${MUSL_DEPS_DIR_STATIC}/expat-src")
    set(EXPAT_SOURCE_DIR "${EXPAT_DOWNLOAD_DIR}/expat-2.6.4")

    if(NOT EXISTS "${EXPAT_SOURCE_DIR}")
        file(MAKE_DIRECTORY "${EXPAT_DOWNLOAD_DIR}")
        message(STATUS "    Downloading expat 2.6.4...")

        file(DOWNLOAD
            "https://github.com/libexpat/libexpat/releases/download/R_2_6_4/expat-2.6.4.tar.gz"
            "${EXPAT_DOWNLOAD_DIR}/expat-2.6.4.tar.gz"
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
        )
        list(GET DOWNLOAD_STATUS 0 DOWNLOAD_CODE)
        list(GET DOWNLOAD_STATUS 1 DOWNLOAD_MSG)
        if(NOT DOWNLOAD_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download expat: ${DOWNLOAD_MSG}")
        endif()

        message(STATUS "    Verifying expat SHA256...")
        file(SHA256 "${EXPAT_DOWNLOAD_DIR}/expat-2.6.4.tar.gz" EXPAT_SHA256)
        if(NOT EXPAT_SHA256 STREQUAL "fd03b7172b3bd7427a3e7a812063f74754f24542429b634e0db6511b53fb2278")
            message(FATAL_ERROR "expat SHA256 mismatch. Got: ${EXPAT_SHA256}")
        endif()

        message(STATUS "    Extracting expat...")
        execute_process(
            COMMAND tar -xzf "${EXPAT_DOWNLOAD_DIR}/expat-2.6.4.tar.gz" -C "${EXPAT_DOWNLOAD_DIR}"
            RESULT_VARIABLE EXPAT_EXTRACT_RESULT
        )
        if(NOT EXPAT_EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract expat")
        endif()
    endif()

    file(MAKE_DIRECTORY "${EXPAT_PREFIX}" "${EXPAT_PREFIX}/build")

    message(STATUS "    Configuring expat with autoconf...")
    execute_process(
        WORKING_DIRECTORY "${EXPAT_SOURCE_DIR}"
        COMMAND bash -c "CFLAGS='-march=x86-64 -mtune=generic -O2' CC=/usr/bin/musl-gcc ./configure --prefix=${EXPAT_PREFIX} --host=x86_64-linux-musl --disable-shared --enable-static --without-xmlwf"
        RESULT_VARIABLE EXPAT_CONFIG_RESULT
        ERROR_VARIABLE EXPAT_CONFIG_ERROR
    )
    if(NOT EXPAT_CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "expat configure failed:\n${EXPAT_CONFIG_ERROR}")
    endif()

    message(STATUS "    Building expat...")
    execute_process(
        WORKING_DIRECTORY "${EXPAT_SOURCE_DIR}"
        COMMAND make -j${CMAKE_BUILD_PARALLEL_LEVEL}
        RESULT_VARIABLE EXPAT_BUILD_RESULT
        ERROR_VARIABLE EXPAT_BUILD_ERROR
    )
    if(NOT EXPAT_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "expat build failed:\n${EXPAT_BUILD_ERROR}")
    endif()

    message(STATUS "    Installing expat...")
    execute_process(
        WORKING_DIRECTORY "${EXPAT_SOURCE_DIR}"
        COMMAND make install
        RESULT_VARIABLE EXPAT_INSTALL_RESULT
        ERROR_VARIABLE EXPAT_INSTALL_ERROR
    )
    if(NOT EXPAT_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "expat install failed:\n${EXPAT_INSTALL_ERROR}")
    endif()
else()
    message(STATUS "  ${BoldBlue}expat${ColorReset} found in cache: ${BoldMagenta}${EXPAT_PREFIX}/lib/libexpat.a${ColorReset}")
endif()

set(EXPAT_LIBRARIES "${EXPAT_PREFIX}/lib/libexpat.a")
set(EXPAT_INCLUDE_DIRS "${EXPAT_PREFIX}/include")
file(MAKE_DIRECTORY "${EXPAT_PREFIX}/include" "${EXPAT_PREFIX}/lib")

# =============================================================================
# wayland - Display server protocol library
# =============================================================================
message(STATUS "Configuring ${BoldBlue}wayland${ColorReset} from source...")

set(WAYLAND_PREFIX "${MUSL_DEPS_DIR_STATIC}/wayland")

if(NOT EXISTS "${WAYLAND_PREFIX}/lib/libwayland-client.a")
    message(STATUS "  wayland library not found, will build from source")

    set(WL_DOWNLOAD_DIR "${MUSL_DEPS_DIR_STATIC}/wayland-src")
    set(WL_SOURCE_DIR "${WL_DOWNLOAD_DIR}/wayland")

    if(NOT EXISTS "${WL_SOURCE_DIR}")
        file(MAKE_DIRECTORY "${WL_DOWNLOAD_DIR}")
        message(STATUS "    Cloning wayland...")
        execute_process(
            COMMAND git clone --depth 1 --branch 1.22.0 https://gitlab.freedesktop.org/wayland/wayland.git "${WL_SOURCE_DIR}"
            RESULT_VARIABLE CLONE_RESULT
        )
        if(NOT CLONE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to clone wayland")
        endif()
    endif()

    set(WL_BUILD_DIR "${WL_DOWNLOAD_DIR}/build")
    file(MAKE_DIRECTORY "${WL_BUILD_DIR}")

    message(STATUS "    Configuring wayland with meson...")
    set(WL_PKG_CONFIG_LIBDIR "${WAYLAND_PROTOCOLS_PREFIX}/share/pkgconfig:${LIBFFI_PREFIX}/lib/pkgconfig:${LIBXML2_PREFIX}/lib/pkgconfig:${EXPAT_PREFIX}/lib/pkgconfig")

    execute_process(
        WORKING_DIRECTORY "${WL_DOWNLOAD_DIR}"
        COMMAND bash -c "CC=${MUSL_GCC} CXX=clang++ PKG_CONFIG_LIBDIR=${WL_PKG_CONFIG_LIBDIR} PKG_CONFIG_PATH= /usr/sbin/meson setup ${WL_BUILD_DIR} ${WL_SOURCE_DIR} --prefix=${WAYLAND_PREFIX} --buildtype=release --default-library=static -Ddocumentation=false -Dtests=false"
        RESULT_VARIABLE WL_CONFIG_RESULT
        ERROR_VARIABLE WL_CONFIG_ERROR
        OUTPUT_VARIABLE WL_CONFIG_OUTPUT
    )
    if(NOT WL_CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "wayland meson setup failed:\n${WL_CONFIG_ERROR}")
    endif()

    message(STATUS "    Building wayland...")
    execute_process(
        COMMAND ${MESON_EXECUTABLE} compile -C "${WL_BUILD_DIR}"
        RESULT_VARIABLE WL_BUILD_RESULT
        ERROR_VARIABLE WL_BUILD_ERROR
    )
    if(NOT WL_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "wayland build failed:\n${WL_BUILD_ERROR}")
    endif()

    message(STATUS "    Installing wayland...")
    execute_process(
        COMMAND ${MESON_EXECUTABLE} install -C "${WL_BUILD_DIR}"
        RESULT_VARIABLE WL_INSTALL_RESULT
        ERROR_VARIABLE WL_INSTALL_ERROR
    )
    if(NOT WL_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "wayland install failed:\n${WL_INSTALL_ERROR}")
    endif()

    message(STATUS "    ${BoldGreen}wayland${ColorReset} built successfully")
else()
    message(STATUS "  ${BoldBlue}wayland${ColorReset} found in cache: ${BoldMagenta}${WAYLAND_PREFIX}/lib/libwayland-client.a${ColorReset}")
endif()

set(WAYLAND_LIBRARIES "${WAYLAND_PREFIX}/lib/libwayland-client.a")
set(WAYLAND_INCLUDE_DIRS "${WAYLAND_PREFIX}/include")
file(MAKE_DIRECTORY "${WAYLAND_PREFIX}/include" "${WAYLAND_PREFIX}/lib")

# Build GTK4 with meson at configure time
if(NOT EXISTS "${GTK4_LIBRARIES}")
    message(STATUS "  GTK4 library not found, will build from source")

    find_program(MESON_EXECUTABLE meson)
    if(NOT MESON_EXECUTABLE)
        message(FATAL_ERROR "meson not found - required to build GTK4")
    endif()

    # FIRST: Build glib if not already built (GTK4 depends on it)
    if(NOT EXISTS "${GLIB_LIBRARIES}")
        message(STATUS "  Building glib (dependency of GTK4)...")

        set(GLIB_DOWNLOAD_DIR "${MUSL_DEPS_DIR_STATIC}/glib-src")
        set(GLIB_TARBALL "${GLIB_DOWNLOAD_DIR}/glib-2.82.0.tar.xz")
        set(GLIB_SOURCE_DIR "${GLIB_DOWNLOAD_DIR}/glib-2.82.0")

        file(MAKE_DIRECTORY "${GLIB_DOWNLOAD_DIR}")

        if(NOT EXISTS "${GLIB_SOURCE_DIR}")
            if(NOT EXISTS "${GLIB_TARBALL}")
                message(STATUS "    Downloading glib...")
                file(DOWNLOAD
                    "https://download.gnome.org/sources/glib/2.82/glib-2.82.0.tar.xz"
                    "${GLIB_TARBALL}"
                    EXPECTED_HASH SHA256=f4c82ada51366bddace49d7ba54b33b4e4d6067afa3008e4847f41cb9b5c38d3
                    SHOW_PROGRESS
                )
            endif()

            message(STATUS "    Extracting glib...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xJf "${GLIB_TARBALL}"
                WORKING_DIRECTORY "${GLIB_DOWNLOAD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract glib")
            endif()
        endif()

        set(GLIB_BUILD_DIR "${GLIB_DOWNLOAD_DIR}/build")
        file(MAKE_DIRECTORY "${GLIB_BUILD_DIR}")

        # Create a meson native file to disable problematic dependencies for musl
        set(GLIB_NATIVE_FILE "${GLIB_DOWNLOAD_DIR}/musl-native.txt")
        file(WRITE "${GLIB_NATIVE_FILE}"
            "[binaries]\n"
            "c = '/usr/bin/musl-gcc'\n"
            "cpp = 'clang++'\n"
            "\n"
            "[properties]\n"
            "c_args = ['-O2', '-fPIC']\n"
        )

        message(STATUS "    Configuring glib...")
        set(GLIB_PKG_CONFIG_LIBDIR "${PCRE2_PREFIX}/lib/pkgconfig:${LIBFFI_PREFIX}/lib/pkgconfig:${ZLIB_PREFIX}/lib/pkgconfig")
        execute_process(
            WORKING_DIRECTORY "${GLIB_DOWNLOAD_DIR}"
            COMMAND bash -c "CC=${MUSL_GCC} CXX=clang++ PKG_CONFIG_LIBDIR=${GLIB_PKG_CONFIG_LIBDIR} PKG_CONFIG_PATH= /usr/sbin/meson setup ${GLIB_BUILD_DIR} ${GLIB_SOURCE_DIR} --prefix=${GLIB_PREFIX} --buildtype=release --default-library=static --wrap-mode=nofallback --native-file=${GLIB_NATIVE_FILE} -Dintrospection=disabled"
            RESULT_VARIABLE GLIB_CONFIG_RESULT
            ERROR_VARIABLE GLIB_CONFIG_ERROR
        )
        if(NOT GLIB_CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "glib meson setup failed:\n${GLIB_CONFIG_ERROR}")
        endif()

        message(STATUS "    Building glib...")
        execute_process(
            WORKING_DIRECTORY "${GLIB_BUILD_DIR}"
            COMMAND /usr/sbin/meson compile
            RESULT_VARIABLE GLIB_BUILD_RESULT
            ERROR_VARIABLE GLIB_BUILD_ERROR
        )
        if(NOT GLIB_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "glib build failed:\n${GLIB_BUILD_ERROR}")
        endif()

        message(STATUS "    Installing glib...")
        execute_process(
            WORKING_DIRECTORY "${GLIB_BUILD_DIR}"
            COMMAND /usr/sbin/meson install
            RESULT_VARIABLE GLIB_INSTALL_RESULT
            ERROR_VARIABLE GLIB_INSTALL_ERROR
        )
        if(NOT GLIB_INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "glib install failed:\n${GLIB_INSTALL_ERROR}")
        endif()

        message(STATUS "    ${BoldGreen}glib${ColorReset} built successfully")
    endif()

    # THEN: Download GTK4
    set(GTK4_DOWNLOAD_DIR "${MUSL_DEPS_DIR_STATIC}/gtk4-src")
    set(GTK4_TARBALL "${GTK4_DOWNLOAD_DIR}/gtk-4.14.1.tar.xz")
    set(GTK4_SOURCE_DIR "${GTK4_DOWNLOAD_DIR}/gtk-4.14.1")

    file(MAKE_DIRECTORY "${GTK4_DOWNLOAD_DIR}")

    if(NOT EXISTS "${GTK4_SOURCE_DIR}")
        if(NOT EXISTS "${GTK4_TARBALL}")
            message(STATUS "  Downloading GTK4...")
            file(DOWNLOAD
                "https://download.gnome.org/sources/gtk/4.14/gtk-4.14.1.tar.xz"
                "${GTK4_TARBALL}"
                EXPECTED_HASH SHA256=fcefb3f132f8cc4711a9efa5b353c9ae9bb5eeff0246fa74dbc2f2f839b9e308
                STATUS DOWNLOAD_STATUS
                SHOW_PROGRESS
            )
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                message(FATAL_ERROR "Failed to download GTK4: ${ERROR_MSG}")
            endif()
        endif()

        message(STATUS "  Extracting GTK4...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xJf "${GTK4_TARBALL}"
            WORKING_DIRECTORY "${GTK4_DOWNLOAD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract GTK4")
        endif()
    endif()

    # GTK4 will be built by ExternalProject_Add below (deferred to build phase)
    message(STATUS "  GTK4 library not found, will build from source via ExternalProject_Add")
else()
    message(STATUS "  ${BoldBlue}GTK4${ColorReset} found in cache: ${GTK4_LIBRARIES}")
endif()

# =============================================================================
# glib - Core utility library (foundation for GTK ecosystem)
# =============================================================================
message(STATUS "Configuring ${BoldBlue}glib${ColorReset} from source...")

set(GLIB_PREFIX "${MUSL_DEPS_DIR_STATIC}/glib")
set(GLIB_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/glib-build")

if(NOT EXISTS "${GLIB_PREFIX}/lib/libglib-2.0.a")
    message(STATUS "  glib library not found in cache, will build from source")

    # Check if meson is available
    find_program(MESON_EXECUTABLE meson)
    if(NOT MESON_EXECUTABLE)
        message(WARNING "meson not found - skipping glib. Install with: sudo apt install meson")
    else()
        # glib requires a cross file for musl builds
        set(GLIB_CROSS_FILE "${GLIB_BUILD_DIR}/musl-cross.ini")
        file(MAKE_DIRECTORY "${GLIB_BUILD_DIR}")
        file(WRITE "${GLIB_CROSS_FILE}"
"[properties]
c_args = ['-O2', '-fPIC', '-I${KERNEL_HEADERS_DIR}']
cpp_args = ['-O2', '-fPIC', '-I${KERNEL_HEADERS_DIR}']

[binaries]
c = '${MUSL_GCC}'
cpp = 'clang++'
ar = '${CMAKE_AR}'
strip = 'strip'
pkg-config = 'pkg-config'
"
        )

        ExternalProject_Add(glib-musl
            URL https://download.gnome.org/sources/glib/2.78/glib-2.78.1.tar.xz
            URL_HASH SHA256=915bc3d0f8507d650ead3832e2f8fb670fce59aac4d7754a7dab6f1e6fed78b2
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${GLIB_BUILD_DIR}
            STAMP_DIR ${GLIB_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND env CC=${MUSL_GCC} CXX=clang++ PKG_CONFIG_PATH=${GLIB_PREFIX}/lib/pkgconfig ${MESON_EXECUTABLE} setup
                <BINARY_DIR>
                <SOURCE_DIR>
                --prefix=${GLIB_PREFIX}
                --buildtype=release
                --default-library=static
                --wrap-mode=nodownload
                -Dintrospection=disabled
                -Dselinux=disabled
                -Dlibmount=disabled
                -Dlibsysprof_capture=disabled
                -Dtests=false
                -Dbsearch_tests=false
                --cross-file=${GLIB_CROSS_FILE}
            BUILD_COMMAND ${MESON_EXECUTABLE} compile -C <BINARY_DIR> -j16
            INSTALL_COMMAND ${MESON_EXECUTABLE} install -C <BINARY_DIR>
            BUILD_BYPRODUCTS
                ${GLIB_PREFIX}/lib/libglib-2.0.a
                ${GLIB_PREFIX}/lib/libgobject-2.0.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )

        set(GLIB_LIBRARIES "${GLIB_PREFIX}/lib/libglib-2.0.a;${GLIB_PREFIX}/lib/libgobject-2.0.a")
        set(GLIB_INCLUDE_DIRS "${GLIB_PREFIX}/include/glib-2.0;${GLIB_PREFIX}/lib/glib-2.0/include")
    endif()
else()
    message(STATUS "  ${BoldBlue}glib${ColorReset} library found in cache: ${BoldMagenta}${GLIB_PREFIX}/lib/libglib-2.0.a${ColorReset}")
    add_custom_target(glib-musl)
    set(GLIB_LIBRARIES "${GLIB_PREFIX}/lib/libglib-2.0.a;${GLIB_PREFIX}/lib/libgobject-2.0.a")
    set(GLIB_INCLUDE_DIRS "${GLIB_PREFIX}/include/glib-2.0;${GLIB_PREFIX}/lib/glib-2.0/include")
endif()

file(MAKE_DIRECTORY "${GLIB_PREFIX}/include" "${GLIB_PREFIX}/lib")

# =============================================================================
# pixman - Pixel manipulation library
# =============================================================================
message(STATUS "Configuring ${BoldBlue}pixman${ColorReset} from source...")

set(PIXMAN_PREFIX "${MUSL_DEPS_DIR_STATIC}/pixman")
set(PIXMAN_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/pixman-build")

if(NOT EXISTS "${PIXMAN_PREFIX}/lib/libpixman-1.a")
    message(STATUS "  pixman library not found in cache, will build from source")
    ExternalProject_Add(pixman-musl
        URL https://www.x.org/releases/individual/lib/pixman-0.42.2.tar.gz
        URL_HASH SHA256=ea1480efada2fd948bc75366f7c349e1c96d3297d09a3fe62626e38e234a625e
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${PIXMAN_BUILD_DIR}
        STAMP_DIR ${PIXMAN_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=${MUSL_GCC} CFLAGS=-O2\ -fPIC <SOURCE_DIR>/configure --prefix=${PIXMAN_PREFIX} --enable-static --disable-shared --disable-libpng
        BUILD_COMMAND env CFLAGS=-O2\ -fPIC make -j
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${PIXMAN_PREFIX}/lib/libpixman-1.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}pixman${ColorReset} library found in cache: ${BoldMagenta}${PIXMAN_PREFIX}/lib/libpixman-1.a${ColorReset}")
    add_custom_target(pixman-musl)
endif()

set(PIXMAN_LIBRARIES "${PIXMAN_PREFIX}/lib/libpixman-1.a")
set(PIXMAN_INCLUDE_DIRS "${PIXMAN_PREFIX}/include")
file(MAKE_DIRECTORY "${PIXMAN_PREFIX}/include" "${PIXMAN_PREFIX}/lib")

# =============================================================================
# freetype - Font rendering engine
# =============================================================================
message(STATUS "Configuring ${BoldBlue}freetype${ColorReset} from source...")

set(FREETYPE_PREFIX "${MUSL_DEPS_DIR_STATIC}/freetype")
set(FREETYPE_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/freetype-build")

if(NOT EXISTS "${FREETYPE_PREFIX}/lib/libfreetype.a")
    message(STATUS "  freetype library not found in cache, will build from source")
    ExternalProject_Add(freetype-musl
        URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-2.tar.gz
        URL_HASH SHA256=427201f5d5151670d05c1f5b45bef5dda1f2e7dd971ef54f0feaaa7ffd2ab90c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${FREETYPE_BUILD_DIR}
        STAMP_DIR ${FREETYPE_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CMAKE_ARGS
            -DCMAKE_TOOLCHAIN_FILE=${MUSL_TOOLCHAIN_FILE}
            -DMUSL_GCC_PATH=${MUSL_GCC}
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
            -DCMAKE_INSTALL_PREFIX=${FREETYPE_PREFIX}
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES=
            -DCMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES=
            -DCMAKE_C_FLAGS=-nostdinc\ -isystem\ /usr/lib/musl/include\ -O2\ -fPIC
            -DFT_DISABLE_PNG=ON
            -DFT_DISABLE_ZLIB=ON
            -DFT_DISABLE_BZIP2=ON
            -DFT_DISABLE_HARFBUZZ=ON
            -DFT_DISABLE_BROTLI=ON
        BUILD_BYPRODUCTS ${FREETYPE_PREFIX}/lib/libfreetype.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}freetype${ColorReset} library found in cache: ${BoldMagenta}${FREETYPE_PREFIX}/lib/libfreetype.a${ColorReset}")
    add_custom_target(freetype-musl)
endif()

set(FREETYPE_LIBRARIES "${FREETYPE_PREFIX}/lib/libfreetype.a")
set(FREETYPE_INCLUDE_DIRS "${FREETYPE_PREFIX}/include")
file(MAKE_DIRECTORY "${FREETYPE_PREFIX}/include" "${FREETYPE_PREFIX}/lib")

# =============================================================================
# cairo - 2D graphics library
# =============================================================================
message(STATUS "Configuring ${BoldBlue}cairo${ColorReset} from source...")

set(CAIRO_PREFIX "${MUSL_DEPS_DIR_STATIC}/cairo")
set(CAIRO_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/cairo-build")

if(NOT EXISTS "${CAIRO_PREFIX}/lib/libcairo.a")
    message(STATUS "  cairo library not found in cache, will build from source")
    ExternalProject_Add(cairo-musl
        URL https://cairographics.org/releases/cairo-1.18.2.tar.xz
        URL_HASH SHA256=a62b9bb42425e844cc3d6ddde043ff39dbabedd1542eba57a2eb79f85889d45a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR cairo-musl
        PREFIX ${CAIRO_BUILD_DIR}
        STAMP_DIR ${CAIRO_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=${MUSL_GCC} PKG_CONFIG_PATH=${PIXMAN_PREFIX}/lib/pkgconfig:${FREETYPE_PREFIX}/lib/pkgconfig:${ZLIB_PREFIX}/lib/pkgconfig meson setup <BINARY_DIR> <SOURCE_DIR> --prefix=${CAIRO_PREFIX} -Ddefault_library=static -Dgtk_doc=false -Dtests=disabled -Dxlib=disabled -Dxcb=disabled -Dfontconfig=disabled
        BUILD_COMMAND meson compile -C <BINARY_DIR> -j16
        INSTALL_COMMAND meson install -C <BINARY_DIR>
        DEPENDS pixman-musl freetype-musl zlib-musl
        BUILD_BYPRODUCTS ${CAIRO_PREFIX}/lib/libcairo.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}cairo${ColorReset} library found in cache: ${BoldMagenta}${CAIRO_PREFIX}/lib/libcairo.a${ColorReset}")
    add_custom_target(cairo-musl DEPENDS pixman-musl)
endif()

set(CAIRO_LIBRARIES "${CAIRO_PREFIX}/lib/libcairo.a")
set(CAIRO_INCLUDE_DIRS "${CAIRO_PREFIX}/include")
file(MAKE_DIRECTORY "${CAIRO_PREFIX}/include" "${CAIRO_PREFIX}/lib")

# =============================================================================
# harfbuzz - Text shaping engine
# =============================================================================
message(STATUS "Configuring ${BoldBlue}harfbuzz${ColorReset} from source...")

set(HARFBUZZ_PREFIX "${MUSL_DEPS_DIR_STATIC}/harfbuzz")
set(HARFBUZZ_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/harfbuzz-build")

if(NOT EXISTS "${HARFBUZZ_PREFIX}/lib/libharfbuzz.a")
    message(STATUS "  harfbuzz library not found in cache, will build from source")

    find_program(MESON_EXECUTABLE meson)
    if(NOT MESON_EXECUTABLE)
        message(WARNING "meson not found - skipping harfbuzz")
    else()
        set(HARFBUZZ_CROSS_FILE "${HARFBUZZ_BUILD_DIR}/musl-cross.ini")
        file(MAKE_DIRECTORY "${HARFBUZZ_BUILD_DIR}")
        file(WRITE "${HARFBUZZ_CROSS_FILE}"
"[properties]
c_args = ['-O2', '-fPIC', '-I${KERNEL_HEADERS_DIR}']
cpp_args = ['-O2', '-fPIC', '-I${KERNEL_HEADERS_DIR}']

[binaries]
c = '${MUSL_GCC}'
cpp = 'clang++'
ar = '${CMAKE_AR}'
strip = 'strip'
"
        )

        ExternalProject_Add(harfbuzz-musl
            URL https://github.com/harfbuzz/harfbuzz/archive/refs/tags/8.3.1.tar.gz
            URL_HASH SHA256=19a54fe9596f7a47c502549fce8e8a10978c697203774008cc173f8360b19a9a
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${HARFBUZZ_BUILD_DIR}
            STAMP_DIR ${HARFBUZZ_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND env CC=${MUSL_GCC} CXX=clang++ PKG_CONFIG_PATH=${FREETYPE_PREFIX}/lib/pkgconfig ${MESON_EXECUTABLE} setup
                <BINARY_DIR>
                <SOURCE_DIR>
                --prefix=${HARFBUZZ_PREFIX}
                --buildtype=release
                --default-library=static
                --wrap-mode=nodownload
                -Dtests=disabled
                -Ddocs=disabled
                -Dintrospection=disabled
                -Dcpp_std=c++17
                --cross-file=${HARFBUZZ_CROSS_FILE}
            BUILD_COMMAND ${MESON_EXECUTABLE} compile -C <BINARY_DIR> -j16
            INSTALL_COMMAND ${MESON_EXECUTABLE} install -C <BINARY_DIR>
            DEPENDS freetype-musl
            BUILD_BYPRODUCTS ${HARFBUZZ_PREFIX}/lib/libharfbuzz.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )

        set(HARFBUZZ_LIBRARIES "${HARFBUZZ_PREFIX}/lib/libharfbuzz.a")
        set(HARFBUZZ_INCLUDE_DIRS "${HARFBUZZ_PREFIX}/include")
    endif()
else()
    message(STATUS "  ${BoldBlue}harfbuzz${ColorReset} library found in cache: ${BoldMagenta}${HARFBUZZ_PREFIX}/lib/libharfbuzz.a${ColorReset}")
    add_custom_target(harfbuzz-musl DEPENDS freetype-musl)
    set(HARFBUZZ_LIBRARIES "${HARFBUZZ_PREFIX}/lib/libharfbuzz.a")
    set(HARFBUZZ_INCLUDE_DIRS "${HARFBUZZ_PREFIX}/include")
endif()

file(MAKE_DIRECTORY "${HARFBUZZ_PREFIX}/include" "${HARFBUZZ_PREFIX}/lib")

# =============================================================================
# pango - Text layout and rendering
# =============================================================================
message(STATUS "Configuring ${BoldBlue}pango${ColorReset} from source...")

set(PANGO_PREFIX "${MUSL_DEPS_DIR_STATIC}/pango")
set(PANGO_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/pango-build")

if(NOT EXISTS "${PANGO_PREFIX}/lib/libpango-1.0.a")
    message(STATUS "  pango library not found in cache, will build from source")

    find_program(MESON_EXECUTABLE meson)
    if(NOT MESON_EXECUTABLE)
        message(WARNING "meson not found - skipping pango")
    else()
        set(PANGO_CROSS_FILE "${PANGO_BUILD_DIR}/musl-cross.ini")
        file(MAKE_DIRECTORY "${PANGO_BUILD_DIR}")
        file(WRITE "${PANGO_CROSS_FILE}"
"[properties]
c_args = ['-O2', '-fPIC', '-I${KERNEL_HEADERS_DIR}']
cpp_args = ['-O2', '-fPIC', '-I${KERNEL_HEADERS_DIR}']

[binaries]
c = '${MUSL_GCC}'
cpp = 'clang++'
ar = '${CMAKE_AR}'
strip = 'strip'
pkg-config = 'pkg-config'
"
        )

        ExternalProject_Add(pango-musl
            URL https://download.gnome.org/sources/pango/1.54/pango-1.54.0.tar.xz
            URL_HASH SHA256=8a9eed75021ee734d7fc0fdf3a65c3bba51dfefe4ae51a9b414a60c70b2d1ed8
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${PANGO_BUILD_DIR}
            STAMP_DIR ${PANGO_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND env CC=${MUSL_GCC} CXX=clang++ PKG_CONFIG_PATH=${GLIB_PREFIX}/lib/pkgconfig:${HARFBUZZ_PREFIX}/lib/pkgconfig:${CAIRO_PREFIX}/lib/pkgconfig:${FREETYPE_PREFIX}/lib/pkgconfig ${MESON_EXECUTABLE} setup
                <BINARY_DIR>
                <SOURCE_DIR>
                --prefix=${PANGO_PREFIX}
                --buildtype=release
                --default-library=static
                --wrap-mode=forcefallback
                -Dintrospection=disabled
                -Dbuild-testsuite=false
                -Ddocumentation=false
                --cross-file=${PANGO_CROSS_FILE}
            BUILD_COMMAND ${MESON_EXECUTABLE} compile -C <BINARY_DIR> -j16
            INSTALL_COMMAND ${MESON_EXECUTABLE} install -C <BINARY_DIR>
            DEPENDS glib-musl harfbuzz-musl cairo-musl
            BUILD_BYPRODUCTS
                ${PANGO_PREFIX}/lib/libpango-1.0.a
                ${PANGO_PREFIX}/lib/libpangocairo-1.0.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )

        set(PANGO_LIBRARIES "${PANGO_PREFIX}/lib/libpango-1.0.a;${PANGO_PREFIX}/lib/libpangocairo-1.0.a")
        set(PANGO_INCLUDE_DIRS "${PANGO_PREFIX}/include/pango-1.0")
    endif()
else()
    message(STATUS "  ${BoldBlue}pango${ColorReset} library found in cache: ${BoldMagenta}${PANGO_PREFIX}/lib/libpango-1.0.a${ColorReset}")
    add_custom_target(pango-musl DEPENDS glib-musl harfbuzz-musl cairo-musl)
    set(PANGO_LIBRARIES "${PANGO_PREFIX}/lib/libpango-1.0.a;${PANGO_PREFIX}/lib/libpangocairo-1.0.a")
    set(PANGO_INCLUDE_DIRS "${PANGO_PREFIX}/include/pango-1.0")
endif()

file(MAKE_DIRECTORY "${PANGO_PREFIX}/include" "${PANGO_PREFIX}/lib")

# =============================================================================
# GTK4 - GTK UI toolkit (statically linked)
# =============================================================================
message(STATUS "Configuring ${BoldBlue}GTK4${ColorReset} from source...")

set(GTK4_PREFIX "${MUSL_DEPS_DIR_STATIC}/gtk4")
set(GTK4_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/gtk4-build")

if(NOT EXISTS "${GTK4_PREFIX}/lib/libgtk-4.a")
    message(STATUS "  GTK4 library not found in cache, will build from source")
    message(STATUS "  This will take several minutes on first build...")

    find_program(MESON_EXECUTABLE meson)
    if(NOT MESON_EXECUTABLE)
        message(WARNING "meson not found - skipping GTK4. Install with: sudo apt install meson")
    else()
        set(GTK4_CROSS_FILE "${GTK4_BUILD_DIR}/musl-cross.ini")
        file(MAKE_DIRECTORY "${GTK4_BUILD_DIR}")
        file(WRITE "${GTK4_CROSS_FILE}"
"[properties]
c_args = ['-O2', '-fPIC', '-I${KERNEL_HEADERS_DIR}']
cpp_args = ['-O2', '-fPIC', '-I${KERNEL_HEADERS_DIR}']

[binaries]
c = '${MUSL_GCC}'
cpp = 'clang++'
ar = '${CMAKE_AR}'
strip = 'strip'
pkg-config = 'pkg-config'
"
        )

        # Set PKG_CONFIG_PATH to include all GTK4 dependencies (cache only, no system paths)
        set(GTK4_PKG_CONFIG_PATH "${PANGO_PREFIX}/lib/pkgconfig:${GLIB_PREFIX}/lib/pkgconfig:${CAIRO_PREFIX}/lib/pkgconfig:${HARFBUZZ_PREFIX}/lib/pkgconfig:${FREETYPE_PREFIX}/lib/pkgconfig:${WAYLAND_PREFIX}/lib/pkgconfig:${PIXMAN_PREFIX}/lib/pkgconfig:${WAYLAND_PROTOCOLS_PREFIX}/share/pkgconfig:${XKBCOMMON_PREFIX}/lib/pkgconfig:${LIBXML2_PREFIX}/lib/pkgconfig:${EXPAT_PREFIX}/lib/pkgconfig:${LIBFFI_PREFIX}/lib/pkgconfig:${PCRE2_PREFIX}/lib/pkgconfig:${ZLIB_PREFIX}/lib/pkgconfig")

        ExternalProject_Add(gtk4-musl
            URL https://download.gnome.org/sources/gtk/4.14/gtk-4.14.1.tar.xz
            URL_HASH SHA256=fcefb3f132f8cc4711a9efa5b353c9ae9bb5eeff0246fa74dbc2f2f839b9e308
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${GTK4_BUILD_DIR}
            STAMP_DIR ${GTK4_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND bash -c "CC=${MUSL_GCC} CXX=clang++ PATH=${GLIB_PREFIX}/bin:$PATH PKG_CONFIG_PATH=${GTK4_PKG_CONFIG_PATH} /usr/sbin/meson setup <BINARY_DIR> <SOURCE_DIR> --prefix=${GTK4_PREFIX} --buildtype=release --default-library=static --wrap-mode=nofallback -Dintrospection=disabled -Dx11-backend=false -Dwin32-backend=false -Dwayland-backend=true"
            BUILD_COMMAND ${MESON_EXECUTABLE} compile -C <BINARY_DIR> -j16
            INSTALL_COMMAND ${MESON_EXECUTABLE} install -C <BINARY_DIR>
            DEPENDS pango-musl cairo-musl pixman-musl harfbuzz-musl freetype-musl glib-musl
            BUILD_BYPRODUCTS ${GTK4_PREFIX}/lib/libgtk-4.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )

        set(GTK4_LIBRARIES "${GTK4_PREFIX}/lib/libgtk-4.a")
        set(GTK4_INCLUDE_DIRS "${GTK4_PREFIX}/include/gtk-4.0")
    endif()
else()
    message(STATUS "  ${BoldBlue}GTK4${ColorReset} library found in cache: ${BoldMagenta}${GTK4_PREFIX}/lib/libgtk-4.a${ColorReset}")
    add_custom_target(gtk4-musl DEPENDS pango-musl cairo-musl pixman-musl harfbuzz-musl freetype-musl glib-musl)
    set(GTK4_LIBRARIES "${GTK4_PREFIX}/lib/libgtk-4.a")
    set(GTK4_INCLUDE_DIRS "${GTK4_PREFIX}/include/gtk-4.0")
endif()

file(MAKE_DIRECTORY "${GTK4_PREFIX}/include" "${GTK4_PREFIX}/lib")

# Restore output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_SAVED_ARCHIVE_OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_SAVED_LIBRARY_OUTPUT_DIR})

message(STATUS "All musl dependencies configured and cached")
