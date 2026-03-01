# =============================================================================
# libsodium Cryptography Library Configuration
# =============================================================================
# Finds and configures libsodium cryptography library
#
# Platform-specific dependency management:
#   - Windows: Uses vcpkg
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

    set(LIBSODIUM_LIBRARIES "${LIBSODIUM_PREFIX}/lib/libsodium.a" PARENT_SCOPE)
    set(LIBSODIUM_INCLUDE_DIRS "${LIBSODIUM_PREFIX}/include" PARENT_SCOPE)
    set(LIBSODIUM_FOUND TRUE PARENT_SCOPE)
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
