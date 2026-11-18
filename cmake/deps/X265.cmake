# =============================================================================
# x265 HEVC Encoder Library Configuration
# =============================================================================
# Finds and configures x265 for HEVC encoding support in FFmpeg
#
# Platform-specific dependency management:
#   - musl: Built from source with the selected Clang musl target
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Windows: Uses vcpkg
#
# Output: `x265::x265` and `asciichat-x265`; `X265_PREFIX` is retained as
# install metadata for FFmpeg's autotools configure step.
# =============================================================================

set(X265_VERSION "4.1")
set(X265_HASH "7d23cdcdbd510728202c0dfbf7c51eda26a395de2096c504c2b10d6035711102")

# =============================================================================
# Windows: x265 not needed (vcpkg FFmpeg is built without libx265)
# =============================================================================
if(WIN32)
    message(STATUS "⚠ x265 not needed on Windows (vcpkg FFmpeg uses built-in codecs)")
    add_library(asciichat-x265 INTERFACE)
    return()
endif()

if(EMSCRIPTEN)
    message(STATUS "⚠ x265 not needed for Emscripten web targets")
    add_library(asciichat-x265 INTERFACE)
    return()
endif()

# =============================================================================
# musl: Build from source
# =============================================================================
if(USE_MUSL)
    # GCC's host libstdc++/glibc toolchain cannot produce a musl-compatible
    # x265 archive on this platform: GCC 16 emits glibc __isoc23_* and libmvec
    # references from x265's C++ sources. Clang's musl libc++ path remains
    # supported; GCC+musl uses FFmpeg's built-in codecs instead.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(STATUS "x265 disabled for GCC musl builds (host C++ runtime is not musl-compatible)")
        add_library(asciichat-x265 INTERFACE)
        return()
    endif()
    include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
    function(asciichat_build_x265)
        asciichat_dependency_defaults()
        set(_prefix "${ASCIICHAT_DEPS_CACHE_DIR}/build/installed/x265")
        set(CMAKE_INSTALL_PREFIX "${_prefix}")
        set(CMAKE_CXX_STANDARD 14)
        set(ENABLE_SHARED OFF)
        set(ENABLE_CLI OFF)
        set(ENABLE_ASSEMBLY OFF)
        set(ENABLE_LIBNUMA OFF)
        asciichat_fetchcontent_declare(asciichat_x265
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/x265"
            URL "https://bitbucket.org/multicoreware/x265_git/get/${X265_VERSION}.tar.gz"
            URL_HASH "SHA256=${X265_HASH}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE EXCLUDE_FROM_ALL)
        FetchContent_GetProperties(asciichat_x265)
        if(NOT asciichat_x265_POPULATED)
            cmake_policy(PUSH)
            if(POLICY CMP0169)
                cmake_policy(SET CMP0169 OLD)
            endif()
            FetchContent_Populate(asciichat_x265)
            cmake_policy(POP)
        endif()
        set(X265_SOURCE_DIR "${asciichat_x265_SOURCE_DIR}")
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/patch-x265-cmake.cmake")
        add_subdirectory("${asciichat_x265_SOURCE_DIR}/source" "${asciichat_x265_BINARY_DIR}" EXCLUDE_FROM_ALL)
        target_include_directories(x265-static PUBLIC
            "$<BUILD_INTERFACE:${asciichat_x265_SOURCE_DIR}/source>"
            "$<BUILD_INTERFACE:${asciichat_x265_BINARY_DIR}>")
        add_custom_target(asciichat-x265-stage
            COMMAND "${CMAKE_COMMAND}" --install "${asciichat_x265_BINARY_DIR}" --prefix "${_prefix}"
            COMMAND "${CMAKE_COMMAND}" "-DPREFIX=${_prefix}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/FixX265PkgConfig.cmake"
            DEPENDS x265-static VERBATIM)
        set(X265_PREFIX "${_prefix}" PARENT_SCOPE)
    endfunction()
    asciichat_build_x265()
    if(NOT TARGET asciichat-x265)
        add_library(asciichat-x265 INTERFACE)
        target_link_libraries(asciichat-x265 INTERFACE x265-static)
    endif()
    return()
endif()

# =============================================================================
# Non-musl: Use system package via pkg-config
# =============================================================================
include(FindPkgConfig)
pkg_check_modules(_asciichat_x265 REQUIRED x265)

if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT BUILD_SHARED_LIBS)
    find_library(_asciichat_x265_static_library
        NAMES libx265.a
        PATHS ${_asciichat_x265_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )
    if(NOT _asciichat_x265_static_library)
        message(FATAL_ERROR "Static x265 is required for portable macOS Release builds")
    endif()
    set(_asciichat_x265_link "${_asciichat_x265_static_library}")
    message(STATUS "${BoldGreen}✓${ColorReset} x265 (static): ${_asciichat_x265_static_library}")
else()
    set(_asciichat_x265_link ${_asciichat_x265_LDFLAGS})
    message(STATUS "${BoldGreen}✓${ColorReset} x265 found")
endif()

if(NOT TARGET x265::x265)
    add_library(x265::x265 INTERFACE IMPORTED)
    target_include_directories(x265::x265 SYSTEM INTERFACE ${_asciichat_x265_INCLUDE_DIRS})
    target_link_libraries(x265::x265 INTERFACE ${_asciichat_x265_link})
endif()
if(NOT TARGET asciichat-x265)
    add_library(asciichat-x265 INTERFACE)
    target_link_libraries(asciichat-x265 INTERFACE x265::x265)
endif()
