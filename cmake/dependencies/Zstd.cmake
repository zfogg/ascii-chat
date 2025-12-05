# =============================================================================
# zstd Compression Library Configuration
# =============================================================================
# Finds and configures zstd compression library
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
#   - ZSTD_LIBRARIES: Libraries to link against
#   - ZSTD_INCLUDE_DIRS: Include directories
#   - ZSTD_FOUND: Whether zstd was found
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

# On macOS, prefer Homebrew zstd over system zstd for consistency
if(APPLE AND NOT USE_MUSL AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    if(EXISTS "/usr/local/opt/zstd/lib/pkgconfig/libzstd.pc")
        set(ENV{PKG_CONFIG_PATH} "/usr/local/opt/zstd/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    elseif(EXISTS "/opt/homebrew/opt/zstd/lib/pkgconfig/libzstd.pc")
        set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/zstd/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
endif()

find_dependency_library(
    NAME ZSTD
    VCPKG_NAMES zstd zstd_static
    HEADER zstd.h
    PKG_CONFIG libzstd
    HOMEBREW_PKG zstd
    STATIC_LIB_NAME libzstd.a
    STATIC_DEFINE ZSTD_STATIC
    REQUIRED
)
