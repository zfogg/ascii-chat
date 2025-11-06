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
#   - VCPKG_ROOT, VCPKG_TRIPLET: (Windows only) vcpkg configuration
#
# Outputs (variables set by this file):
#   - LIBSODIUM_LIBRARIES: Libraries to link against
#   - LIBSODIUM_INCLUDE_DIRS: Include directories
#   - LIBSODIUM_FOUND: Whether libsodium was found
# =============================================================================

if(WIN32)
    # Windows: Find libsodium from vcpkg
    find_library(LIBSODIUM_LIBRARY_RELEASE NAMES libsodium sodium PATHS "${VCPKG_LIB_PATH}" NO_DEFAULT_PATH)
    find_library(LIBSODIUM_LIBRARY_DEBUG NAMES libsodium sodium PATHS "${VCPKG_DEBUG_LIB_PATH}" NO_DEFAULT_PATH)
    find_path(LIBSODIUM_INCLUDE_DIR NAMES sodium.h PATHS "${VCPKG_INCLUDE_PATH}" NO_DEFAULT_PATH)

    if(LIBSODIUM_LIBRARY_RELEASE OR LIBSODIUM_LIBRARY_DEBUG)
        set(LIBSODIUM_LIBRARIES optimized ${LIBSODIUM_LIBRARY_RELEASE} debug ${LIBSODIUM_LIBRARY_DEBUG})
        set(LIBSODIUM_INCLUDE_DIRS ${LIBSODIUM_INCLUDE_DIR})
        set(LIBSODIUM_FOUND TRUE)
        message(STATUS "Found ${BoldGreen}libsodium${ColorReset}: ${LIBSODIUM_LIBRARY_RELEASE}")

        # Define SODIUM_STATIC for static builds to prevent dllimport
        # vcpkg x64-windows-static triplet uses static libraries for all build types
        if(VCPKG_TRIPLET MATCHES "static")
            add_compile_definitions(SODIUM_STATIC)
        endif()
    else()
        message(WARNING "Could not find ${BoldRed}libsodium${ColorReset} - will continue without encryption")
        set(LIBSODIUM_FOUND FALSE)
        set(LIBSODIUM_LIBRARIES "")
        set(LIBSODIUM_INCLUDE_DIRS "")
    endif()
else()
    # Unix/Linux/macOS: Use pkg-config
    # Skip pkg-config when using musl - dependencies are built from source
    if(NOT USE_MUSL)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBSODIUM REQUIRED libsodium)
    endif()
endif()
