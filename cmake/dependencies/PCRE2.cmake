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
#   - For musl: Built from source in MuslDependencies.cmake
#   - For vcpkg: Uses vcpkg-installed PCRE2
#   - Otherwise: Uses system-installed PCRE2 via pkg-config
#
# Prerequisites (must be set before including this file):
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

# Skip for musl builds - PCRE2 is configured in MuslDependencies.cmake
if(USE_MUSL)
    return()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

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
