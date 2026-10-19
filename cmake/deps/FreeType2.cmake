# =============================================================================
# FreeType2 Dependency
# =============================================================================
# Cross-platform configuration for FreeType2 font rasterization
#
# For musl builds: Built from source
# For iOS builds: Built from source with iOS cross-compilation
# For native builds: Uses system package manager
#
# Outputs (variables set by this file):
#   - FREETYPE_LIBRARIES: FreeType2 libraries to link
#   - FREETYPE_INCLUDE_DIRS: FreeType2 include directories
# =============================================================================


include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
if(PLATFORM_IOS AND NOT BUILD_EXECUTABLES)
    return()
endif()
if(PLATFORM_IOS OR USE_MUSL OR ASCIICHAT_BUILD_DEPS_FROM_SOURCE)
    function(asciichat_build_freetype)
        asciichat_dependency_defaults()
        set(_prefix "${CMAKE_CURRENT_BINARY_DIR}/_deps/installed/freetype")
        if(USE_MUSL OR PLATFORM_IOS)
            set(CMAKE_INSTALL_PREFIX "${_prefix}")
        endif()
        set(FT_DISABLE_PNG ON)
        set(FT_DISABLE_ZLIB ON)
        set(FT_DISABLE_BZIP2 ON)
        set(FT_DISABLE_HARFBUZZ ON)
        set(FT_DISABLE_BROTLI ON)
        set(ZLIB_FOUND FALSE)
        set(BZIP2_FOUND FALSE)
        set(PNG_FOUND FALSE)
        set(HARFBUZZ_FOUND FALSE)
        set(BROTLIDEC_FOUND FALSE)
        FetchContent_Declare(asciichat_freetype
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/asciichat_freetype"
            EXCLUDE_FROM_ALL
            URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-2.tar.gz
            URL_HASH SHA256=427201f5d5151670d05c1f5b45bef5dda1f2e7dd971ef54f0feaaa7ffd2ab90c
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(asciichat_freetype)
        set(FREETYPE_INCLUDE_DIRS "${asciichat_freetype_SOURCE_DIR}/include;${asciichat_freetype_BINARY_DIR}/include" PARENT_SCOPE)
        # Fontconfig's Autotools build consumes an installation, not CMake targets.
        add_custom_target(asciichat-freetype-stage
            COMMAND "${CMAKE_COMMAND}" --install "${asciichat_freetype_BINARY_DIR}" --prefix "${_prefix}"
            DEPENDS freetype VERBATIM)
        set(FREETYPE_PREFIX "${_prefix}" PARENT_SCOPE)
    endfunction()
    asciichat_build_freetype()
    set(FREETYPE_LIBRARIES freetype)
    return()
endif()

# Non-musl builds: Use system package manager
if(UNIX AND NOT APPLE)
    # Linux/BSD: Try pkg-config first, then fall back to CMake FindFreetype
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FREETYPE freetype2)
    endif()

    # If pkg-config didn't work, try CMake's FindFreetype
    if(NOT FREETYPE_FOUND)
        find_package(Freetype REQUIRED)
    endif()

    message(STATUS "${BoldGreen}✓${ColorReset} FreeType2: ${FREETYPE_LIBRARIES}")

elseif(APPLE)
    # macOS: Try pkg-config first, then fall back to CMake FindFreetype
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FREETYPE freetype2)
    endif()

    # If pkg-config didn't work, try CMake's FindFreetype
    if(NOT FREETYPE_FOUND)
        find_package(Freetype REQUIRED)
    endif()

    # Homebrew's dylib is versioned to the installed formula and is not a
    # suitable runtime dependency for a portable Release executable.
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_SHARED_DEPS)
        find_file(FREETYPE_STATIC_ARCHIVE NAMES libfreetype.a
            PATHS "${HOMEBREW_PREFIX}/opt/freetype/lib" /usr/local/opt/freetype/lib /opt/homebrew/opt/freetype/lib
            NO_DEFAULT_PATH)
        if(NOT FREETYPE_STATIC_ARCHIVE)
            message(FATAL_ERROR "macOS Release requires the static Homebrew FreeType archive (libfreetype.a)")
        endif()
        set(FREETYPE_LIBRARIES "${FREETYPE_STATIC_ARCHIVE}")
    endif()

    message(STATUS "${BoldGreen}✓${ColorReset} FreeType2: ${FREETYPE_LIBRARIES}")

elseif(WIN32)
    # Windows: Use vcpkg
    find_package(freetype CONFIG REQUIRED)
    # Set FREETYPE_LIBRARIES for compatibility with lib/CMakeLists.txt
    set(FREETYPE_LIBRARIES freetype)
    # Get include dirs from the target
    get_target_property(FREETYPE_INCLUDE_DIRS freetype INTERFACE_INCLUDE_DIRECTORIES)

    message(STATUS "${BoldGreen}✓${ColorReset} FreeType2: vcpkg")

else()
    message(FATAL_ERROR "Unsupported platform for FreeType2")
endif()
