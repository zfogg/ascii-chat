# =============================================================================
# MuslDependencies.cmake - Build all dependencies from source for musl
# =============================================================================
# When USE_MUSL=ON, we can't use system libraries (glibc-based). Instead, we
# build all dependencies from source and cache them in .deps-cache-musl/.
#
# Dependencies built from source:
#   - zstd (compression)
#   - libsodium (crypto)
#   - PortAudio (audio I/O)
#   - BearSSL (TLS for SSH key fetching)
#
# All cached in ${FETCHCONTENT_BASE_DIR} to persist across build/ deletions.
# =============================================================================

if(NOT USE_MUSL)
    return()
endif()

message(STATUS "Building dependencies from source for musl libc...")

include(FetchContent)

# Save current output directories
set(_SAVED_ARCHIVE_OUTPUT_DIR ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
set(_SAVED_LIBRARY_OUTPUT_DIR ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

# Use cache directory for musl deps - both PREFIX and install dir
set(MUSL_PREFIX "${FETCHCONTENT_BASE_DIR}/musl-deps")
message(STATUS "MuslDependencies.cmake: FETCHCONTENT_BASE_DIR=${FETCHCONTENT_BASE_DIR}")
message(STATUS "MuslDependencies.cmake: MUSL_PREFIX=${MUSL_PREFIX}")

# =============================================================================
# Copy kernel headers to project-local directory
# =============================================================================
# Clang with musl doesn't include kernel headers by default. We need linux/, asm/,
# and asm-generic/ headers for ALSA and V4L2. Copy them to a project-local
# directory so the build works on fresh installs without system modifications.

set(KERNEL_HEADERS_DIR "${MUSL_PREFIX}/kernel-headers")

# Find kernel headers from common locations
set(KERNEL_HEADER_SEARCH_PATHS
    "/usr/include/linux"
    "/usr/include/x86_64-linux-gnu/asm"
    "/usr/include/asm"
    "/usr/include/asm-generic"
)

# Check if kernel headers exist
set(KERNEL_HEADERS_FOUND FALSE)
foreach(HEADER_PATH ${KERNEL_HEADER_SEARCH_PATHS})
    if(EXISTS "${HEADER_PATH}")
        set(KERNEL_HEADERS_FOUND TRUE)
        break()
    endif()
endforeach()

if(NOT KERNEL_HEADERS_FOUND)
    message(WARNING "Kernel headers not found in common locations. Install linux-libc-dev or kernel-headers package.")
else()
    # Copy kernel headers only if they don't exist in cache
    if(NOT EXISTS "${KERNEL_HEADERS_DIR}/linux")
        message(STATUS "Copying kernel headers to ${KERNEL_HEADERS_DIR}...")
        file(MAKE_DIRECTORY "${KERNEL_HEADERS_DIR}")

        # Copy linux/ headers
        if(EXISTS "/usr/include/linux")
            file(COPY "/usr/include/linux" DESTINATION "${KERNEL_HEADERS_DIR}")
        endif()

        # Copy asm/ headers (try arch-specific first, then generic)
        if(EXISTS "/usr/include/x86_64-linux-gnu/asm")
            file(COPY "/usr/include/x86_64-linux-gnu/asm" DESTINATION "${KERNEL_HEADERS_DIR}")
        elseif(EXISTS "/usr/include/asm")
            file(COPY "/usr/include/asm" DESTINATION "${KERNEL_HEADERS_DIR}")
        endif()

        # Copy asm-generic/ headers
        if(EXISTS "/usr/include/asm-generic")
            file(COPY "/usr/include/asm-generic" DESTINATION "${KERNEL_HEADERS_DIR}")
        endif()

        message(STATUS "Kernel headers copied successfully")
    else()
        message(STATUS "Using cached kernel headers from ${KERNEL_HEADERS_DIR}")
    endif()

    # Set CFLAGS to include kernel headers for ALSA and PortAudio builds
    set(MUSL_KERNEL_CFLAGS "-fPIC -I${KERNEL_HEADERS_DIR}")
endif()

# =============================================================================
# zstd - Compression library
# =============================================================================
message(STATUS "Configuring zstd from source...")

# Only add external project if library doesn't exist
if(NOT EXISTS "${MUSL_PREFIX}/lib/libzstd.a")
    message(STATUS "  zstd library not found in cache, will build from source")
    ExternalProject_Add(zstd-musl
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        PREFIX ${FETCHCONTENT_BASE_DIR}/zstd-musl
        STAMP_DIR ${FETCHCONTENT_BASE_DIR}/zstd-musl/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND ""
        BUILD_COMMAND env CC=/usr/bin/musl-gcc REALGCC=/usr/bin/gcc CFLAGS=-fPIC make -C <SOURCE_DIR> lib-release PREFIX=${MUSL_PREFIX}
        INSTALL_COMMAND make -C <SOURCE_DIR> install PREFIX=${MUSL_PREFIX}
        BUILD_IN_SOURCE 1
        BUILD_BYPRODUCTS ${MUSL_PREFIX}/lib/libzstd.a
    )
else()
    message(STATUS "  zstd library found in cache: ${MUSL_PREFIX}/lib/libzstd.a")
    # Create a dummy target so dependencies can reference it
    add_custom_target(zstd-musl)
endif()

set(ZSTD_FOUND TRUE)
set(ZSTD_LIBRARIES "${MUSL_PREFIX}/lib/libzstd.a")
set(ZSTD_INCLUDE_DIRS "${MUSL_PREFIX}/include")

# =============================================================================
# libsodium - Cryptography library
# =============================================================================
message(STATUS "Configuring libsodium from source...")

# Only add external project if library doesn't exist
if(NOT EXISTS "${MUSL_PREFIX}/lib/libsodium.a")
    message(STATUS "  libsodium library not found in cache, will build from source")
    ExternalProject_Add(libsodium-musl
        URL https://github.com/jedisct1/libsodium/releases/download/1.0.20-RELEASE/libsodium-1.0.20.tar.gz
        URL_HASH SHA256=ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19
        PREFIX ${FETCHCONTENT_BASE_DIR}/libsodium-musl
        STAMP_DIR ${FETCHCONTENT_BASE_DIR}/libsodium-musl/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=/usr/bin/musl-gcc REALGCC=/usr/bin/gcc CFLAGS=-fPIC <SOURCE_DIR>/configure --prefix=${MUSL_PREFIX} --enable-static --disable-shared
        BUILD_COMMAND env REALGCC=/usr/bin/gcc make
        INSTALL_COMMAND make install
        DEPENDS zstd-musl
        BUILD_BYPRODUCTS ${MUSL_PREFIX}/lib/libsodium.a
    )
else()
    message(STATUS "  libsodium library found in cache: ${MUSL_PREFIX}/lib/libsodium.a")
    # Create a dummy target so dependencies can reference it
    add_custom_target(libsodium-musl DEPENDS zstd-musl)
endif()

set(LIBSODIUM_FOUND TRUE)
set(LIBSODIUM_LIBRARIES "${MUSL_PREFIX}/lib/libsodium.a")
set(LIBSODIUM_INCLUDE_DIRS "${MUSL_PREFIX}/include")

# =============================================================================
# ALSA - Advanced Linux Sound Architecture
# =============================================================================
message(STATUS "Configuring ALSA from source...")

include(ExternalProject)

# Only add external project if library doesn't exist
if(NOT EXISTS "${MUSL_PREFIX}/lib/libasound.a")
    message(STATUS "  ALSA library not found in cache, will build from source")
    ExternalProject_Add(alsa-lib-musl
        URL https://www.alsa-project.org/files/pub/lib/alsa-lib-1.2.12.tar.bz2
        URL_HASH SHA256=4868cd908627279da5a634f468701625be8cc251d84262c7e5b6a218391ad0d2
        PREFIX ${FETCHCONTENT_BASE_DIR}/alsa-lib-musl
        STAMP_DIR ${FETCHCONTENT_BASE_DIR}/alsa-lib-musl/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=/usr/bin/musl-gcc REALGCC=/usr/bin/gcc CFLAGS=${MUSL_KERNEL_CFLAGS} <SOURCE_DIR>/configure --prefix=${MUSL_PREFIX} --enable-static --disable-shared --disable-maintainer-mode
        BUILD_COMMAND env REALGCC=/usr/bin/gcc make
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${MUSL_PREFIX}/lib/libasound.a
    )
else()
    message(STATUS "  ALSA library found in cache: ${MUSL_PREFIX}/lib/libasound.a")
    # Create a dummy target so dependencies can reference it
    add_custom_target(alsa-lib-musl)
endif()

# Set ALSA variables for PortAudio to find
set(ALSA_FOUND TRUE)
set(ALSA_LIBRARIES "${MUSL_PREFIX}/lib/libasound.a")
set(ALSA_INCLUDE_DIRS "${MUSL_PREFIX}/include")

# =============================================================================
# PortAudio - Audio I/O library
# =============================================================================
message(STATUS "Configuring PortAudio from source...")

# Only add external project if library doesn't exist
if(NOT EXISTS "${MUSL_PREFIX}/lib/libportaudio.a")
    message(STATUS "  PortAudio library not found in cache, will build from source")
    ExternalProject_Add(portaudio-musl
        URL http://files.portaudio.com/archives/pa_stable_v190700_20210406.tgz
        URL_HASH SHA256=47efbf42c77c19a05d22e627d42873e991ec0c1357219c0d74ce6a2948cb2def
        PREFIX ${FETCHCONTENT_BASE_DIR}/portaudio-musl
        STAMP_DIR ${FETCHCONTENT_BASE_DIR}/portaudio-musl/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=/usr/bin/musl-gcc REALGCC=/usr/bin/gcc CFLAGS=-fPIC PKG_CONFIG_PATH=${MUSL_PREFIX}/lib/pkgconfig <SOURCE_DIR>/configure --prefix=${MUSL_PREFIX} --enable-static --disable-shared --with-alsa --without-jack --without-oss
        BUILD_COMMAND env REALGCC=/usr/bin/gcc make
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${MUSL_PREFIX}/lib/libportaudio.a
        DEPENDS alsa-lib-musl
    )
else()
    message(STATUS "  PortAudio library found in cache: ${MUSL_PREFIX}/lib/libportaudio.a")
    # Create a dummy target so dependencies can reference it
    add_custom_target(portaudio-musl DEPENDS alsa-lib-musl)
endif()

set(PORTAUDIO_FOUND TRUE)
set(PORTAUDIO_LIBRARIES "${MUSL_PREFIX}/lib/libportaudio.a")
set(PORTAUDIO_INCLUDE_DIRS "${MUSL_PREFIX}/include")

# =============================================================================
# libexecinfo - Backtrace support for musl
# =============================================================================
message(STATUS "Configuring libexecinfo from source...")

# Only add external project if library doesn't exist
if(NOT EXISTS "${MUSL_PREFIX}/lib/libexecinfo.a")
    message(STATUS "  libexecinfo library not found in cache, will build from source")
    ExternalProject_Add(libexecinfo-musl
        GIT_REPOSITORY https://github.com/mikroskeem/libexecinfo.git
        GIT_TAG master
        PREFIX ${FETCHCONTENT_BASE_DIR}/libexecinfo-musl
        STAMP_DIR ${FETCHCONTENT_BASE_DIR}/libexecinfo-musl/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND ""
        BUILD_COMMAND env CC=/usr/bin/musl-gcc REALGCC=/usr/bin/gcc CFLAGS=-fPIC make -C <SOURCE_DIR>
        INSTALL_COMMAND make -C <SOURCE_DIR> install PREFIX=${MUSL_PREFIX}
        BUILD_IN_SOURCE 1
        BUILD_BYPRODUCTS ${MUSL_PREFIX}/lib/libexecinfo.a
    )
else()
    message(STATUS "  libexecinfo library found in cache: ${MUSL_PREFIX}/lib/libexecinfo.a")
    # Create a dummy target so dependencies can reference it
    add_custom_target(libexecinfo-musl)
endif()

set(LIBEXECINFO_FOUND TRUE)
set(LIBEXECINFO_LIBRARIES "${MUSL_PREFIX}/lib/libexecinfo.a")
set(LIBEXECINFO_INCLUDE_DIRS "${MUSL_PREFIX}/include")

# =============================================================================
# BearSSL - TLS library for SSH key fetching
# =============================================================================
message(STATUS "Configuring BearSSL from source...")

# BearSSL doesn't use CMake, so we build it manually
set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/bearssl")
set(BEARSSL_BUILD_DIR "${FETCHCONTENT_BASE_DIR}/bearssl-build-${CMAKE_BUILD_TYPE}")
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
        execute_process(
            COMMAND make static CC=/usr/bin/musl-gcc AR=${CMAKE_AR} CFLAGS=-DBR_USE_GETENTROPY=0\ -DBR_USE_URANDOM=1\ -U_FORTIFY_SOURCE\ -D_FORTIFY_SOURCE=0\ -fno-stack-protector\ -fPIC
            WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
            RESULT_VARIABLE BEARSSL_MAKE_RESULT
            OUTPUT_VARIABLE BEARSSL_MAKE_OUTPUT
            ERROR_VARIABLE BEARSSL_MAKE_ERROR
        )

        if(BEARSSL_MAKE_RESULT EQUAL 0)
            # Copy library to cache
            file(COPY "${BEARSSL_SOURCE_DIR}/build/libbearssl.a"
                 DESTINATION "${BEARSSL_BUILD_DIR}")
            message(STATUS "BearSSL library built and cached successfully")
        else()
            message(FATAL_ERROR "BearSSL build failed with exit code ${BEARSSL_MAKE_RESULT}\nOutput: ${BEARSSL_MAKE_OUTPUT}\nError: ${BEARSSL_MAKE_ERROR}")
        endif()
    else()
        message(STATUS "Using cached BearSSL library: ${BEARSSL_LIB}")
    endif()

    set(BEARSSL_FOUND TRUE)
    set(BEARSSL_LIBRARIES "${BEARSSL_LIB}")
    set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SOURCE_DIR}/inc")
else()
    message(WARNING "BearSSL submodule not found - GitHub/GitLab key fetching will be disabled")
    set(BEARSSL_FOUND FALSE)
    set(BEARSSL_LIBRARIES "")
    set(BEARSSL_INCLUDE_DIRS "")
endif()

# Restore output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_SAVED_ARCHIVE_OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_SAVED_LIBRARY_OUTPUT_DIR})

message(STATUS "All musl dependencies configured and cached")
