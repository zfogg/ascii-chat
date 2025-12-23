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
#   - Opus (audio codec)
#   - speexdsp (audio DSP for echo cancellation)
#   - BearSSL (TLS for SSH key fetching)
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
        BUILD_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC make -C <SOURCE_DIR> lib-release PREFIX=${ZSTD_PREFIX}
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

set(ZSTD_FOUND TRUE)
set(ZSTD_LIBRARIES "${ZSTD_PREFIX}/lib/libzstd.a")
set(ZSTD_INCLUDE_DIRS "${ZSTD_PREFIX}/include")

# =============================================================================
# libsodium - Cryptography library
# =============================================================================
message(STATUS "Configuring ${BoldBlue}libsodium${ColorReset} from source...")

set(LIBSODIUM_PREFIX "${MUSL_DEPS_DIR_STATIC}/libsodium")
set(LIBSODIUM_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libsodium-build")

# Only add external project if library doesn't exist
if(NOT EXISTS "${LIBSODIUM_PREFIX}/lib/libsodium.a")
    message(STATUS "  libsodium library not found in cache, will build from source")
    ExternalProject_Add(libsodium-musl
        URL https://github.com/jedisct1/libsodium/releases/download/1.0.20-RELEASE/libsodium-1.0.20.tar.gz
        URL_HASH SHA256=ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${LIBSODIUM_BUILD_DIR}
        STAMP_DIR ${LIBSODIUM_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC <SOURCE_DIR>/configure --prefix=${LIBSODIUM_PREFIX} --enable-static --disable-shared
        BUILD_COMMAND env REALGCC=${REAL_GCC} make
        INSTALL_COMMAND make install
        DEPENDS zstd-musl
        BUILD_BYPRODUCTS ${LIBSODIUM_PREFIX}/lib/libsodium.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}libsodium${ColorReset} library found in cache: ${BoldMagenta}${LIBSODIUM_PREFIX}/lib/libsodium.a${ColorReset}")
    # Create a dummy target so dependencies can reference it
    add_custom_target(libsodium-musl DEPENDS zstd-musl)
endif()

set(LIBSODIUM_FOUND TRUE)
set(LIBSODIUM_LIBRARIES "${LIBSODIUM_PREFIX}/lib/libsodium.a")
set(LIBSODIUM_INCLUDE_DIRS "${LIBSODIUM_PREFIX}/include")

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
        BUILD_COMMAND env REALGCC=${REAL_GCC} make
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
set(ALSA_FOUND TRUE)
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
        BUILD_COMMAND env REALGCC=${REAL_GCC} make
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

set(PORTAUDIO_FOUND TRUE)
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
        CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC <SOURCE_DIR>/configure --prefix=${OPUS_PREFIX} --enable-static --disable-shared --disable-doc
        BUILD_COMMAND env REALGCC=${REAL_GCC} make
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

set(OPUS_FOUND TRUE)
set(OPUS_LIBRARIES "${OPUS_PREFIX}/lib/libopus.a")
set(OPUS_INCLUDE_DIRS "${OPUS_PREFIX}/include")

# =============================================================================
# speexdsp - Audio DSP library for echo cancellation
# =============================================================================
message(STATUS "Configuring ${BoldBlue}speexdsp${ColorReset} from source...")

set(SPEEXDSP_PREFIX "${MUSL_DEPS_DIR_STATIC}/speexdsp")
set(SPEEXDSP_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/speexdsp-build")

# Only add external project if library doesn't exist
if(NOT EXISTS "${SPEEXDSP_PREFIX}/lib/libspeexdsp.a")
    message(STATUS "  speexdsp library not found in cache, will build from source")
    ExternalProject_Add(speexdsp-musl
        URL https://api.github.com/repos/xiph/speexdsp/tarball/SpeexDSP-1.2.1
        URL_HASH SHA256=33dc01fb91d951ebc07d505cf83533f46b69cabafccf801cdc57a6679e257284
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${SPEEXDSP_BUILD_DIR}
        STAMP_DIR ${SPEEXDSP_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        PATCH_COMMAND bash -c "cd '<SOURCE_DIR>' && for d in xiph-speexdsp-*; do [ -d \"$d\" ] && (mv \"$d\"/* . 2>/dev/null; rmdir \"$d\") || true; done && ls -la"
        CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC <SOURCE_DIR>/configure --prefix=${SPEEXDSP_PREFIX} --enable-static --disable-shared
        BUILD_COMMAND env REALGCC=${REAL_GCC} make
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${SPEEXDSP_PREFIX}/lib/libspeexdsp.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}speexdsp${ColorReset} library found in cache: ${BoldMagenta}${SPEEXDSP_PREFIX}/lib/libspeexdsp.a${ColorReset}")
    # Create a dummy target so dependencies can reference it
    add_custom_target(speexdsp-musl)
endif()

set(SPEEXDSP_FOUND TRUE)
set(SPEEXDSP_LIBRARIES "${SPEEXDSP_PREFIX}/lib/libspeexdsp.a")
set(SPEEXDSP_INCLUDE_DIRS "${SPEEXDSP_PREFIX}/include")

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
        BUILD_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC make -C <SOURCE_DIR>
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

set(LIBEXECINFO_FOUND TRUE)
set(LIBEXECINFO_LIBRARIES "${LIBEXECINFO_PREFIX}/lib/libexecinfo.a")
set(LIBEXECINFO_INCLUDE_DIRS "${LIBEXECINFO_PREFIX}/include")

# =============================================================================
# BearSSL - TLS library for SSH key fetching
# =============================================================================
message(STATUS "Configuring ${BoldBlue}BearSSL${ColorReset} from source...")

# BearSSL doesn't use CMake, so we build it manually
set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/bearssl")
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
            COMMAND make -j1 lib CC=${MUSL_GCC} AR=${CMAKE_AR} CFLAGS=-DBR_USE_GETENTROPY=0\ -DBR_USE_URANDOM=1\ -U_FORTIFY_SOURCE\ -D_FORTIFY_SOURCE=0\ -fno-stack-protector\ -fPIC
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

    set(BEARSSL_FOUND TRUE)
    set(BEARSSL_LIBRARIES bearssl_static)
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
