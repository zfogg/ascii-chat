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


include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
if(EMSCRIPTEN OR PLATFORM_IOS OR USE_MUSL OR ASCIICHAT_BUILD_DEPS_FROM_SOURCE)
    function(asciichat_build_pcre2)
        asciichat_dependency_defaults()
        set(BUILD_STATIC_LIBS ON)
        if(BUILD_SHARED_LIBS)
            set(BUILD_STATIC_LIBS OFF)
        endif()
        set(PCRE2_BUILD_PCRE2_8 ON)
        set(PCRE2_BUILD_PCRE2_16 OFF)
        set(PCRE2_BUILD_PCRE2_32 OFF)
        set(PCRE2_BUILD_PCRE2GREP OFF)
        set(PCRE2_BUILD_TESTS OFF)
        set(PCRE2_STATIC_PIC ON)
        if(EMSCRIPTEN)
            set(PCRE2_SUPPORT_JIT OFF)
        endif()
        FetchContent_Declare(asciichat_pcre2
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_pcre2"
            EXCLUDE_FROM_ALL
            URL https://github.com/PCRE2Project/pcre2/archive/refs/tags/pcre2-10.47.tar.gz
            URL_HASH SHA256=409c443549b13b216da40049850a32f3e6c57d4224ab11553ab5a786878a158e
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(asciichat_pcre2)
        if(BUILD_SHARED_LIBS)
            set(_target pcre2-8-shared)
        else()
            set(_target pcre2-8-static)
        endif()
        set(PCRE2_LIBRARIES ${_target} PARENT_SCOPE)
        set(PCRE2_INCLUDE_DIRS "${asciichat_pcre2_BINARY_DIR}/interface" PARENT_SCOPE)
    endfunction()
    asciichat_build_pcre2()
    set(PCRE2_FOUND TRUE)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../utils/FindDependency.cmake)
if(APPLE AND NOT USE_MUSL)
    if(HOMEBREW_PREFIX AND EXISTS "${HOMEBREW_PREFIX}/opt/pcre2/lib/pkgconfig/libpcre2-8.pc")
        set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/opt/pcre2/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
endif()

find_dependency_library(
    NAME PCRE2
    VCPKG_NAMES pcre2-8 pcre2 pcre2-8d pcre2d
    HEADER pcre2.h
    PKG_CONFIG libpcre2-8
    HOMEBREW_PKG pcre2
    STATIC_LIB_NAME libpcre2-8.a
    STATIC_DEFINE PCRE2_STATIC
    REQUIRED
)
