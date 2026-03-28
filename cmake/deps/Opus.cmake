# =============================================================================
# Opus Library Configuration
# =============================================================================
# Finds and configures Opus audio codec library
#
# Opus is used for:
#   - Real-time audio compression for network transmission
#   - Low-latency voice and music encoding/decoding
#   - Efficient bandwidth usage in multi-client audio chat
#
# Build strategy:
#   - For WASM/Emscripten: Built from source with Emscripten toolchain
#   - For iOS: Built from source with iOS cross-compilation
#   - For musl: Built from source
#   - For vcpkg: Uses vcpkg-installed Opus
#   - For glibc: Uses system-installed Opus via pkg-config
#
# Prerequisites (must be set before including this file):
#   - PLATFORM_IOS: Whether building for iOS
#   - USE_MUSL: Whether using musl libc
#   - USE_VCPKG: Whether using vcpkg
#   - CMAKE_BUILD_TYPE: Build type
#   - FETCHCONTENT_BASE_DIR: Shared source cache directory
#
# Outputs (variables set by this file):
#   - OPUS_FOUND: Whether Opus was found
#   - OPUS_LIBRARIES: Libraries to link against
#   - OPUS_INCLUDE_DIRS: Include directories
# =============================================================================

include(FetchContent)

# Shared source URL for autotools builds (iOS, musl, WASM)
FetchContent_Declare(opus-src
    URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
    URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/opus-src"
    UPDATE_DISCONNECTED ON
)

# WASM builds: Use Emscripten toolchain
if(DEFINED EMSCRIPTEN)
    message(STATUS "Configuring ${BoldBlue}Opus${ColorReset} from source (WASM)...")

    include(ExternalProject)
    FetchContent_Populate(opus-src)

    set(OPUS_PREFIX "${FETCHCONTENT_BASE_DIR}/opus-wasm")
    set(OPUS_BUILD_DIR "${FETCHCONTENT_BASE_DIR}/opus-wasm-build")

    if(NOT EXISTS "${OPUS_PREFIX}/lib/libopus.a")
        message(STATUS "  Opus library not found in cache, will build from source")

        ExternalProject_Add(opus-wasm
            SOURCE_DIR ${opus-src_SOURCE_DIR}
            PREFIX ${OPUS_BUILD_DIR}
            STAMP_DIR ${OPUS_BUILD_DIR}/stamps
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND
                <SOURCE_DIR>/configure
                --prefix=${OPUS_PREFIX}
                --host=wasm32-emscripten
                --enable-static
                --disable-shared
                --disable-doc
                --disable-extra-programs
                --disable-intrinsics
                --disable-asm
                --with-pic
                CC=emcc
                CFLAGS=-O2\ -fPIC
                LDFLAGS=--no-entry
            BUILD_COMMAND make -j
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${OPUS_PREFIX}/lib/libopus.a
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}Opus${ColorReset} library found in cache: ${BoldMagenta}${OPUS_PREFIX}/lib/libopus.a${ColorReset}")
        add_custom_target(opus-wasm)
    endif()

    set(OPUS_FOUND TRUE)
    set(OPUS_LIBRARIES "${OPUS_PREFIX}/lib/libopus.a")
    set(OPUS_INCLUDE_DIRS "${OPUS_PREFIX}/include")

    message(STATUS "${BoldGreen}✓${ColorReset} Opus (WASM): ${BoldCyan}libopus${ColorReset}")
    return()
endif()

# iOS build: Build from source for iOS cross-compilation
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}Opus${ColorReset} from source (iOS cross-compile)...")

    include(ExternalProject)
    FetchContent_Populate(opus-src)

    set(OPUS_PREFIX "${IOS_DEPS_CACHE_DIR}/opus")
    set(OPUS_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/opus-build")

    # Get actual iOS SDK path using xcrun
    if(BUILD_IOS_SIM)
        execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    else()
        execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()

    if(NOT EXISTS "${OPUS_PREFIX}/lib/libopus.a")
        message(STATUS "  Opus library not found in cache, will build from source")

        ExternalProject_Add(opus-ios
            SOURCE_DIR ${opus-src_SOURCE_DIR}
            PREFIX ${OPUS_BUILD_DIR}
            STAMP_DIR ${OPUS_BUILD_DIR}/stamps
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND
                <SOURCE_DIR>/configure
                --prefix=${OPUS_PREFIX}
                --enable-static
                --disable-shared
                --disable-doc
                --disable-extra-programs
                --disable-intrinsics
                --disable-asm
                --with-pic
                --host=arm-apple-darwin
                CC=clang
                CFLAGS=-O2\ -fPIC\ -isysroot\ ${IOS_SDK_PATH}\ -arch\ arm64\ -miphoneos-version-min=16.0
                LDFLAGS=-isysroot\ ${IOS_SDK_PATH}\ -arch\ arm64
            BUILD_COMMAND make -j
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${OPUS_PREFIX}/lib/libopus.a
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}Opus${ColorReset} library found in cache: ${BoldMagenta}${OPUS_PREFIX}/lib/libopus.a${ColorReset}")
        add_custom_target(opus-ios)
    endif()

    set(OPUS_FOUND TRUE)
    set(OPUS_LIBRARIES "${OPUS_PREFIX}/lib/libopus.a")
    set(OPUS_INCLUDE_DIRS "${OPUS_PREFIX}/include")

    message(STATUS "${BoldGreen}✓${ColorReset} Opus configured (iOS cross-compile): ${OPUS_PREFIX}/lib/libopus.a")

    return()
endif()

# musl builds - Opus is built from source
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}libopus${ColorReset} from source (musl)...")

    include(ExternalProject)
    FetchContent_Populate(opus-src)

    set(OPUS_PREFIX "${MUSL_DEPS_DIR_STATIC}/opus")
    set(OPUS_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/opus-build")

    # Only add external project if library doesn't exist
    if(NOT EXISTS "${OPUS_PREFIX}/lib/libopus.a")
        message(STATUS "  libopus library not found in cache, will build from source")
        ExternalProject_Add(opus-musl
            SOURCE_DIR ${opus-src_SOURCE_DIR}
            PREFIX ${OPUS_BUILD_DIR}
            STAMP_DIR ${OPUS_BUILD_DIR}/stamps
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=-fPIC <SOURCE_DIR>/configure --prefix=${OPUS_PREFIX} --enable-static --disable-shared --disable-doc --disable-extra-programs
            BUILD_COMMAND env REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${OPUS_PREFIX}/lib/libopus.a
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}libopus${ColorReset} library found in cache: ${BoldMagenta}${OPUS_PREFIX}/lib/libopus.a${ColorReset}")
        add_custom_target(opus-musl)
    endif()

    set(OPUS_FOUND TRUE)
    set(OPUS_LIBRARIES "${OPUS_PREFIX}/lib/libopus.a")
    set(OPUS_INCLUDE_DIRS "${OPUS_PREFIX}/include")
    return()
endif()

# Native builds: Use find_dependency_library helper
include(${CMAKE_CURRENT_LIST_DIR}/../utils/FindDependency.cmake)

find_dependency_library(
    NAME OPUS
    VCPKG_NAMES opus
    HEADER opus/opus.h
    PKG_CONFIG opus
    HOMEBREW_PKG opus
    STATIC_LIB_NAME libopus.a
    REQUIRED
)
