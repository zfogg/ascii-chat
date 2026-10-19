# =============================================================================
# zstd Compression Library Configuration
# =============================================================================
# Finds and configures zstd compression library using CMake's find_package()
#
# Platform-specific dependency management:
#   - WASM/Emscripten: Built from source with CMake
#   - iOS: Built from source with iOS cross-compilation
#   - musl: Built from source with musl toolchain
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Windows: Uses vcpkg
#
# Outputs:
#   - zstd::zstd or zstd::libzstd - Imported target for linking
#   - ZSTD_FOUND - Whether zstd was found/configured
#   - ZSTD_LIBRARIES - Library paths
#   - ZSTD_INCLUDE_DIRS - Include directories
#   - FETCHCONTENT_BASE_DIR: Shared source cache directory
# =============================================================================


include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)

if(EMSCRIPTEN OR PLATFORM_IOS OR USE_MUSL OR ASCIICHAT_BUILD_DEPS_FROM_SOURCE)
    function(asciichat_build_zstd)
        asciichat_dependency_defaults()
        set(ZSTD_BUILD_SHARED ${BUILD_SHARED_LIBS})
        set(ZSTD_BUILD_STATIC ON)
        if(BUILD_SHARED_LIBS)
            set(ZSTD_BUILD_STATIC OFF)
        endif()
        set(ZSTD_BUILD_PROGRAMS OFF)
        set(ZSTD_BUILD_TESTS OFF)
        if(EMSCRIPTEN)
            set(ZSTD_MULTITHREAD_SUPPORT OFF)
        endif()
        FetchContent_Declare(asciichat_zstd
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_zstd"
            EXCLUDE_FROM_ALL
            URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
            URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
            SOURCE_SUBDIR build/cmake DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(asciichat_zstd)
        if(BUILD_SHARED_LIBS)
            set(_target libzstd_shared)
        else()
            set(_target libzstd_static)
        endif()
        target_include_directories(${_target} PUBLIC "$<BUILD_INTERFACE:${asciichat_zstd_SOURCE_DIR}/lib>")
        add_library(zstd::libzstd ALIAS ${_target})
        set(ZSTD_LIBRARIES ${_target} PARENT_SCOPE)
        set(ZSTD_INCLUDE_DIRS "${asciichat_zstd_SOURCE_DIR}/lib" PARENT_SCOPE)
    endfunction()
    asciichat_build_zstd()
    set(ZSTD_FOUND TRUE)
    return()
endif()

# macOS Release: Prefer Homebrew's static archive to avoid a runtime dependency
# on the build machine's zstd dylib version.
if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_SHARED_DEPS)
    include(FindPkgConfig)
    pkg_check_modules(ZSTD_PKG REQUIRED libzstd)
    find_library(ZSTD_STATIC_LIBRARY
        NAMES libzstd.a
        PATHS ${ZSTD_PKG_LIBRARY_DIRS}
        NO_DEFAULT_PATH
    )
    if(NOT ZSTD_STATIC_LIBRARY)
        message(FATAL_ERROR "Static zstd is required for macOS Release builds")
    endif()
    set(ZSTD_LIBRARIES "${ZSTD_STATIC_LIBRARY}")
    set(ZSTD_INCLUDE_DIRS ${ZSTD_PKG_INCLUDE_DIRS})
    if(NOT TARGET zstd::libzstd)
        add_library(zstd::libzstd STATIC IMPORTED GLOBAL)
        set_target_properties(zstd::libzstd PROPERTIES
            IMPORTED_LOCATION "${ZSTD_STATIC_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIRS}"
        )
    endif()
    set(ZSTD_FOUND TRUE)
    message(STATUS "${BoldGreen}✓${ColorReset} zstd (static): ${ZSTD_STATIC_LIBRARY}")
    return()
endif()

# =============================================================================
# Non-musl: Use system package or pkg-config
# =============================================================================

# Try to find zstd via CMake config first, then fall back to pkg-config
find_package(zstd QUIET CONFIG)

if(NOT zstd_FOUND)
    # Fall back to pkg-config if CMake config not found
    include(FindPkgConfig)
    pkg_check_modules(zstd REQUIRED libzstd)

    # Create interface library for compatibility (use zstd::libzstd for consistency)
    if(NOT TARGET zstd::libzstd)
        add_library(zstd::libzstd INTERFACE IMPORTED)
        target_include_directories(zstd::libzstd SYSTEM INTERFACE ${zstd_INCLUDE_DIRS})
        target_link_libraries(zstd::libzstd INTERFACE ${zstd_LIBRARIES})
    endif()

    # Also create zstd::zstd as an alias for backward compatibility
    if(NOT TARGET zstd::zstd)
        add_library(zstd::zstd ALIAS zstd::libzstd)
    endif()
endif()

set(ZSTD_FOUND TRUE)

message(STATUS "${BoldGreen}✓${ColorReset} zstd found")
