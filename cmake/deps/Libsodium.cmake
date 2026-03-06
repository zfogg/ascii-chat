# =============================================================================
# libsodium Cryptography Library Configuration
# =============================================================================
# Finds and configures libsodium cryptography library
#
# Platform-specific dependency management:
#   - Windows: Uses vcpkg
#   - iOS: Built from source with iOS cross-compilation
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
#   - LIBSODIUM_LIBRARIES: Libraries to link against
#   - LIBSODIUM_INCLUDE_DIRS: Include directories
#   - LIBSODIUM_FOUND: Whether libsodium was found
# =============================================================================

# Handle iOS builds - libsodium is built from source
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}libsodium${ColorReset} from source (iOS cross-compile)...")

    include(ExternalProject)

    set(LIBSODIUM_PREFIX "${IOS_DEPS_CACHE_DIR}/libsodium")
    set(LIBSODIUM_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/libsodium-build")

    # Determine iOS SDK path
    if(BUILD_IOS_SIM)
        set(IOS_SDK_PATH "$(xcrun --sdk iphonesimulator --show-sdk-path)")
        set(IOS_SDK_NAME "iphonesimulator")
    else()
        set(IOS_SDK_PATH "$(xcrun --sdk iphoneos --show-sdk-path)")
        set(IOS_SDK_NAME "iphoneos")
    endif()

    if(NOT EXISTS "${LIBSODIUM_PREFIX}/lib/libsodium.a")
        message(STATUS "  libsodium library not found in cache, will build from source")

        ExternalProject_Add(libsodium-ios
            URL https://github.com/jedisct1/libsodium/releases/download/1.0.20-RELEASE/libsodium-1.0.20.tar.gz
            URL_HASH SHA256=ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${LIBSODIUM_BUILD_DIR}
            STAMP_DIR ${LIBSODIUM_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND
                <SOURCE_DIR>/configure
                --prefix=${LIBSODIUM_PREFIX}
                --enable-static
                --disable-shared
                --with-pic
                --host=arm-apple-darwin
                CC=clang
                CFLAGS=-isysroot\ ${IOS_SDK_PATH}\ -arch\ arm64\ -miphoneos-version-min=16.0
                LD=ld
                LDFLAGS=-isysroot\ ${IOS_SDK_PATH}\ -arch\ arm64
            BUILD_COMMAND make -j
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${LIBSODIUM_PREFIX}/lib/libsodium.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}libsodium${ColorReset} library found in cache: ${BoldMagenta}${LIBSODIUM_PREFIX}/lib/libsodium.a${ColorReset}")
        add_custom_target(libsodium-ios)
    endif()

    set(LIBSODIUM_LIBRARIES "${LIBSODIUM_PREFIX}/lib/libsodium.a")
    set(LIBSODIUM_INCLUDE_DIRS "${LIBSODIUM_PREFIX}/include")
    set(LIBSODIUM_FOUND TRUE)
    return()
endif()

# Handle musl builds - libsodium is built from source
if(USE_MUSL)
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
    set(LIBSODIUM_FOUND TRUE)
    return()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

find_dependency_library(
    NAME LIBSODIUM
    VCPKG_NAMES libsodium sodium
    HEADER sodium.h
    PKG_CONFIG libsodium
    HOMEBREW_PKG libsodium
    STATIC_LIB_NAME libsodium.a
    STATIC_DEFINE SODIUM_STATIC
    OPTIONAL
)
