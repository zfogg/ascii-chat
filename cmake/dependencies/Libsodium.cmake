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
