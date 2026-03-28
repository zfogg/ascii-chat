# =============================================================================
# PCRE2 Library Configuration
# =============================================================================
# Finds and configures PCRE2 regular expression library
#
# PCRE2 is used for:
#   - URL validation using production-grade HTTP(S) URL regex
#   - Input validation and pattern matching
#
# Build strategy:
#   - For iOS: Built from source with iOS cross-compilation
#   - For musl: Built from source in MuslDependencies.cmake
#   - For vcpkg: Uses vcpkg-installed PCRE2
#   - Otherwise: Uses system-installed PCRE2 via pkg-config
#
# Prerequisites (must be set before including this file):
#   - PLATFORM_IOS: Whether building for iOS
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - VCPKG_ROOT, VCPKG_TARGET_TRIPLET: (Windows only) vcpkg configuration
#
# Outputs (variables set by this file):
#   - PCRE2_LIBRARIES: Libraries to link against
#   - PCRE2_INCLUDE_DIRS: Include directories
#   - PCRE2_FOUND: Whether PCRE2 was found
# =============================================================================

# iOS build: Build from source for iOS cross-compilation
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}PCRE2${ColorReset} from source (iOS cross-compile)...")

    include(ExternalProject)

    set(PCRE2_PREFIX "${IOS_DEPS_CACHE_DIR}/pcre2")
    set(PCRE2_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/pcre2-build")

    # Get actual iOS SDK path using xcrun
    if(BUILD_IOS_SIM)
        execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    else()
        execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()

    if(NOT EXISTS "${PCRE2_PREFIX}/lib/libpcre2-8.a")
        message(STATUS "  PCRE2 library not found in cache, will build from source")

        ExternalProject_Add(pcre2-ios
            URL https://github.com/PCRE2Project/pcre2/archive/refs/tags/pcre2-10.47.tar.gz
            URL_HASH SHA256=409c443549b13b216da40049850a32f3e6c57d4224ab11553ab5a786878a158e
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${PCRE2_BUILD_DIR}
            STAMP_DIR ${PCRE2_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND
                <SOURCE_DIR>/configure
                --prefix=${PCRE2_PREFIX}
                --enable-static
                --disable-shared
                --enable-pcre2-8
                --disable-pcre2-16
                --disable-pcre2-32
                --disable-maintainer-mode
                --with-pic
                --host=arm-apple-darwin
                CC=clang
                CFLAGS=-O2\ -fPIC\ -isysroot\ ${IOS_SDK_PATH}\ -arch\ arm64\ -miphoneos-version-min=16.0
                LDFLAGS=-isysroot\ ${IOS_SDK_PATH}\ -arch\ arm64
            BUILD_COMMAND make -j
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
        add_custom_target(pcre2-ios)
    endif()

    set(PCRE2_FOUND TRUE)
    set(PCRE2_LIBRARIES "${PCRE2_PREFIX}/lib/libpcre2-8.a")
    set(PCRE2_INCLUDE_DIRS "${PCRE2_PREFIX}/include")

    message(STATUS "${BoldGreen}✓${ColorReset} PCRE2 configured (iOS cross-compile): ${PCRE2_PREFIX}/lib/libpcre2-8.a")

    return()
endif()

# Handle WASM/Emscripten builds - PCRE2 is built from source
if(DEFINED EMSCRIPTEN)
    message(STATUS "Configuring ${BoldBlue}PCRE2${ColorReset} from source (WASM)...")

    include(ExternalProject)
    include(FetchContent)
    FetchContent_Declare(pcre2-src
        URL https://github.com/PCRE2Project/pcre2/archive/refs/tags/pcre2-10.47.tar.gz
        URL_HASH SHA256=409c443549b13b216da40049850a32f3e6c57d4224ab11553ab5a786878a158e
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/pcre2-src"
        UPDATE_DISCONNECTED ON
    )
    FetchContent_Populate(pcre2-src)

    set(PCRE2_PREFIX "${FETCHCONTENT_BASE_DIR}/pcre2-wasm")
    set(PCRE2_BUILD_DIR "${FETCHCONTENT_BASE_DIR}/pcre2-wasm-build")

    if(NOT EXISTS "${PCRE2_PREFIX}/lib/libpcre2-8.a")
        message(STATUS "  PCRE2 library not found in cache, will build from source")

        ExternalProject_Add(pcre2-wasm
            SOURCE_DIR ${pcre2-src_SOURCE_DIR}
            PREFIX ${PCRE2_BUILD_DIR}
            STAMP_DIR ${PCRE2_BUILD_DIR}/stamps
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND
                <SOURCE_DIR>/configure
                --prefix=${PCRE2_PREFIX}
                --host=wasm32-emscripten
                --enable-static
                --disable-shared
                --enable-pcre2-8
                --disable-pcre2-16
                --disable-pcre2-32
                --disable-maintainer-mode
                --with-pic
                CC=emcc
                CFLAGS=-O2\ -fPIC
                LDFLAGS=--no-entry
            BUILD_COMMAND make -j
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${PCRE2_PREFIX}/lib/libpcre2-8.a
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}PCRE2${ColorReset} library found in cache: ${BoldMagenta}${PCRE2_PREFIX}/lib/libpcre2-8.a${ColorReset}")
        add_custom_target(pcre2-wasm)
    endif()

    set(PCRE2_FOUND TRUE)
    set(PCRE2_LIBRARIES "${PCRE2_PREFIX}/lib/libpcre2-8.a")
    set(PCRE2_INCLUDE_DIRS "${PCRE2_PREFIX}/include")

    message(STATUS "${BoldGreen}✓${ColorReset} PCRE2 (WASM): ${BoldCyan}libpcre2-8${ColorReset}")
    return()
endif()

# Handle musl builds - PCRE2 is built from source
if(USE_MUSL)
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

    set(PCRE2_FOUND TRUE)
    set(PCRE2_LIBRARIES "${PCRE2_PREFIX}/lib/libpcre2-8.a")
    set(PCRE2_INCLUDE_DIRS "${PCRE2_PREFIX}/include")
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../utils/FindDependency.cmake)

# On macOS, prefer Homebrew PCRE2 over system PCRE2 for consistency
if(APPLE AND NOT USE_MUSL AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    if(HOMEBREW_PREFIX AND EXISTS "${HOMEBREW_PREFIX}/opt/pcre2/lib/pkgconfig/libpcre2-8.pc")
        set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/opt/pcre2/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
endif()

find_dependency_library(
    NAME PCRE2
    VCPKG_NAMES pcre2-8 pcre2
    HEADER pcre2.h
    PKG_CONFIG libpcre2-8
    HOMEBREW_PKG pcre2
    STATIC_LIB_NAME libpcre2-8.a
    STATIC_DEFINE PCRE2_STATIC
    REQUIRED
)
