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
# Output:
#   - asciichat-opus: canonical INTERFACE target for consumers
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
        asciichat_fetchcontent_declare(asciichat_opus
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/opus"
            EXCLUDE_FROM_ALL
            URL https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
            URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(asciichat_opus)
    endfunction()
    asciichat_build_opus()
    if(NOT TARGET asciichat-opus)
        add_library(asciichat-opus INTERFACE)
        target_link_libraries(asciichat-opus INTERFACE Opus::opus)
        target_include_directories(asciichat-opus INTERFACE "${asciichat_opus_SOURCE_DIR}/include")
    endif()
    return()
endif()

# Native builds: Use find_dependency_library helper
include(${CMAKE_CURRENT_LIST_DIR}/../utils/FindDependency.cmake)

asciichat_find_dependency_target(
    NAME OPUS
    # vcpkg's Windows static port installs libopus.lib, while other
    # platforms and triplets expose the shorter opus library name.
    VCPKG_NAMES opus libopus
    HEADER opus/opus.h
    PKG_CONFIG opus
    HOMEBREW_PKG opus
    STATIC_LIB_NAME libopus.a
    REQUIRED
)

# Match the header layout exported by Opus::opus and pkg-config.
get_target_property(_opus_interface_includes asciichat-opus INTERFACE_INCLUDE_DIRECTORIES)
if(NOT _opus_interface_includes OR _opus_interface_includes STREQUAL "_opus_interface_includes-NOTFOUND")
    set(_opus_interface_includes)
endif()
foreach(_opus_include_candidate IN ITEMS
    ${_opus_interface_includes}
    /usr/include /usr/include/opus /usr/local/include /usr/local/include/opus)
    if(EXISTS "${_opus_include_candidate}/opus.h")
        set(_opus_header_include_dir "${_opus_include_candidate}")
        break()
    elseif(EXISTS "${_opus_include_candidate}/opus/opus.h")
        set(_opus_header_include_dir "${_opus_include_candidate}/opus")
        break()
    endif()
endforeach()
if(NOT _opus_header_include_dir)
    find_path(_opus_header_include_dir NAMES opus.h
        HINTS ${_opus_interface_includes}
        PATH_SUFFIXES opus REQUIRED)
endif()
if(_opus_header_include_dir)
    target_include_directories(asciichat-opus INTERFACE "${_opus_header_include_dir}")
endif()
