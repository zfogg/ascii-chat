# =============================================================================
# x265 HEVC Encoder Library Configuration
# =============================================================================
# Finds and configures x265 for HEVC encoding support in FFmpeg
#
# Platform-specific dependency management:
#   - musl: Built from source with musl-gcc
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Windows: Uses vcpkg
#
# Outputs:
#   - X265_FOUND - Whether x265 was found/configured
#   - X265_LIBRARIES - Library paths
#   - X265_INCLUDE_DIRS - Include directories
#   - X265_PREFIX - Install prefix (for musl builds, passed to FFmpeg configure)
# =============================================================================

set(X265_VERSION "4.1")
set(X265_HASH "7d23cdcdbd510728202c0dfbf7c51eda26a395de2096c504c2b10d6035711102")

# =============================================================================
# Windows: x265 not needed (vcpkg FFmpeg is built without libx265)
# =============================================================================
if(WIN32)
    set(X265_FOUND FALSE)
    message(STATUS "⚠ x265 not needed on Windows (vcpkg FFmpeg uses built-in codecs)")
    return()
endif()

# =============================================================================
# musl: Build from source
# =============================================================================
if(USE_MUSL)
    include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
    function(asciichat_build_x265)
        asciichat_dependency_defaults()
        set(_prefix "${CMAKE_CURRENT_BINARY_DIR}/_deps/installed/x265")
        set(CMAKE_INSTALL_PREFIX "${_prefix}")
        set(CMAKE_CXX_STANDARD 14)
        set(ENABLE_SHARED OFF)
        set(ENABLE_CLI OFF)
        set(ENABLE_ASSEMBLY OFF)
        set(ENABLE_LIBNUMA OFF)
        FetchContent_Declare(asciichat_x265
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_x265"
            URL "https://bitbucket.org/multicoreware/x265_git/get/${X265_VERSION}.tar.gz"
            URL_HASH "SHA256=${X265_HASH}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE EXCLUDE_FROM_ALL
            SOURCE_SUBDIR asciichat-patch-first)
        FetchContent_MakeAvailable(asciichat_x265)
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
        set(X265_INCLUDE_DIRS "${asciichat_x265_SOURCE_DIR}/source;${asciichat_x265_BINARY_DIR}" PARENT_SCOPE)
    endfunction()
    asciichat_build_x265()
    set(X265_LIBRARIES x265-static)
    set(X265_FOUND TRUE)
    return()
endif()

# =============================================================================
# Non-musl: Use system package via pkg-config
# =============================================================================
include(FindPkgConfig)
pkg_check_modules(X265 REQUIRED x265)

if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_SHARED_DEPS)
    find_library(X265_STATIC_LIBRARY
        NAMES libx265.a
        PATHS ${X265_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )
    if(NOT X265_STATIC_LIBRARY)
        message(FATAL_ERROR "Static x265 is required for portable macOS Release builds")
    endif()
    set(X265_LIBRARIES "${X265_STATIC_LIBRARY}")
    message(STATUS "${BoldGreen}✓${ColorReset} x265 (static): ${X265_STATIC_LIBRARY}")
else()
    set(X265_LIBRARIES ${X265_LDFLAGS})
    message(STATUS "${BoldGreen}✓${ColorReset} x265 found")
endif()

if(NOT TARGET x265::x265)
    add_library(x265::x265 INTERFACE IMPORTED)
    target_include_directories(x265::x265 SYSTEM INTERFACE ${X265_INCLUDE_DIRS})
    target_link_libraries(x265::x265 INTERFACE ${X265_LIBRARIES})
endif()

set(X265_FOUND TRUE)
