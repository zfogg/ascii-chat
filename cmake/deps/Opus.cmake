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


include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
if(EMSCRIPTEN OR PLATFORM_IOS OR USE_MUSL OR ASCIICHAT_BUILD_DEPS_FROM_SOURCE)
    function(asciichat_build_opus)
        asciichat_dependency_defaults()
        set(OPUS_BUILD_SHARED_LIBRARY ${BUILD_SHARED_LIBS})
        set(OPUS_BUILD_TESTING OFF)
        set(OPUS_BUILD_PROGRAMS OFF)
        if(EMSCRIPTEN)
            set(OPUS_DISABLE_INTRINSICS ON)
        endif()
        FetchContent_Declare(asciichat_opus
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_opus"
            EXCLUDE_FROM_ALL
            URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
            URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(asciichat_opus)
        set(OPUS_INCLUDE_DIRS "${asciichat_opus_SOURCE_DIR}/include" PARENT_SCOPE)
    endfunction()
    asciichat_build_opus()
    set(OPUS_LIBRARIES Opus::opus)
    set(OPUS_FOUND TRUE)
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

# Match the header layout exported by Opus::opus and pkg-config.
find_path(OPUS_HEADER_INCLUDE_DIR NAMES opus.h
    HINTS ${OPUS_INCLUDE_DIRS} PATH_SUFFIXES opus NO_DEFAULT_PATH REQUIRED)
set(OPUS_INCLUDE_DIRS "${OPUS_HEADER_INCLUDE_DIR}")
