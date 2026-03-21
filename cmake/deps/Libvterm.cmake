# =============================================================================
# libvterm Dependency
# =============================================================================
# Cross-platform configuration for libvterm terminal emulation
#
# For musl builds: libvterm is built from source (FetchContent)
# For native builds: Uses system package manager
#
# Outputs (variables set by this file):
#   - VTERM_LDFLAGS: libvterm libraries to link
#   - VTERM_INCLUDE_DIRS: libvterm include directories
#   - RENDER_FILE_LIBS: Combined libraries for render-file backend
#   - RENDER_FILE_INCLUDES: Combined include directories for render-file backend
#   - GHOSTTY_LIBS: Backwards compatibility alias for RENDER_FILE_LIBS
#   - GHOSTTY_INCLUDES: Backwards compatibility alias for RENDER_FILE_INCLUDES
# =============================================================================
# NOTE: FreeType2 and Fontconfig must be included by Dependencies.cmake before this file

# All builds: Try to find libvterm

# iOS builds: Build from source (only if executables enabled, render-file needs it)
if(PLATFORM_IOS AND BUILD_EXECUTABLES)
    message(STATUS "Configuring ${BoldBlue}libvterm${ColorReset} from source (iOS cross-compile)...")

    include(ExternalProject)

    set(VTERM_PREFIX "${IOS_DEPS_CACHE_DIR}/libvterm")
    set(VTERM_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/libvterm-build")

    # Determine iOS SDK path
    if(BUILD_IOS_SIM)
        set(IOS_SDK_PATH "$(xcrun --sdk iphonesimulator --show-sdk-path)")
    else()
        set(IOS_SDK_PATH "$(xcrun --sdk iphoneos --show-sdk-path)")
    endif()

    if(NOT EXISTS "${VTERM_PREFIX}/lib/libvterm.a")
        message(STATUS "  libvterm library not found in cache, will build from source")

        ExternalProject_Add(libvterm-ios
            GIT_REPOSITORY https://github.com/neovim/libvterm.git
            GIT_TAG v0.3.3
            UPDATE_DISCONNECTED 1
            PREFIX ${VTERM_BUILD_DIR}
            STAMP_DIR ${VTERM_BUILD_DIR}/stamps
            SOURCE_DIR ${VTERM_BUILD_DIR}/src/libvterm-ios
            BINARY_DIR ${VTERM_BUILD_DIR}/src/libvterm-ios
            BUILD_ALWAYS 0
            DEPENDS freetype-ios
            CONFIGURE_COMMAND ""
            BUILD_COMMAND bash -c "cd <SOURCE_DIR> && make CC=clang CFLAGS='-O2 -fPIC -isysroot ${IOS_SDK_PATH} -arch arm64 -miphoneos-version-min=16.0' LDFLAGS='-isysroot ${IOS_SDK_PATH} -arch arm64' ARFLAGS=rcs"
            INSTALL_COMMAND bash -c "cd <SOURCE_DIR> && make install PREFIX=${VTERM_PREFIX}"
            BUILD_BYPRODUCTS ${VTERM_PREFIX}/lib/libvterm.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}libvterm${ColorReset} library found in cache: ${BoldMagenta}${VTERM_PREFIX}/lib/libvterm.a${ColorReset}")
        add_custom_target(libvterm-ios)
    endif()

    set(VTERM_LDFLAGS "${VTERM_PREFIX}/lib/libvterm.a")
    set(VTERM_INCLUDE_DIRS "${VTERM_PREFIX}/include")
    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend (iOS): ${BoldCyan}libvterm + FreeType2${ColorReset}")

    return()
endif()

# iOS without executables: Skip libvterm (not needed, render-file is disabled)
if(PLATFORM_IOS)
    message(STATUS "Skipping libvterm (iOS with BUILD_EXECUTABLES=OFF)")
    return()
endif()

# musl builds: Build from source
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}libvterm${ColorReset} from source (musl)...")

    include(ExternalProject)

    set(VTERM_PREFIX "${MUSL_DEPS_DIR_STATIC}/libvterm")
    set(VTERM_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libvterm-build")

    if(NOT EXISTS "${VTERM_PREFIX}/lib/libvterm.a")
        message(STATUS "  libvterm library not found in cache, will build from source")

        ExternalProject_Add(libvterm-musl
            GIT_REPOSITORY https://github.com/neovim/libvterm.git
            GIT_TAG v0.3.3
            UPDATE_DISCONNECTED 1
            PREFIX ${VTERM_BUILD_DIR}
            STAMP_DIR ${VTERM_BUILD_DIR}/stamps
            SOURCE_DIR ${VTERM_BUILD_DIR}/src/libvterm-musl
            BINARY_DIR ${VTERM_BUILD_DIR}/src/libvterm-musl
            BUILD_ALWAYS 0
            DEPENDS freetype-musl fontconfig
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ${CMAKE_COMMAND} -DMUSL_GCC=${MUSL_GCC} -DKERNEL_HEADERS_DIR=${KERNEL_HEADERS_DIR} -DSOURCE_DIR=<SOURCE_DIR> -P ${CMAKE_SOURCE_DIR}/cmake/scripts/build-libvterm.cmake
            INSTALL_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR> -DPREFIX=${VTERM_PREFIX} -P ${CMAKE_SOURCE_DIR}/cmake/scripts/install-libvterm.cmake
            BUILD_BYPRODUCTS ${VTERM_PREFIX}/lib/libvterm.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}libvterm${ColorReset} library found in cache: ${BoldMagenta}${VTERM_PREFIX}/lib/libvterm.a${ColorReset}")
        add_custom_target(libvterm-musl)
    endif()

    set(VTERM_LDFLAGS "${VTERM_PREFIX}/lib/libvterm.a")
    set(VTERM_INCLUDE_DIRS "${VTERM_PREFIX}/include")
    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2 + fontconfig${ColorReset}")

# Unix builds (Linux/BSD, non-musl): Use system package manager
elseif(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM vterm REQUIRED)

    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2 + fontconfig${ColorReset}")

elseif(APPLE)
    # macOS: Use system package managers (homebrew or macports)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM vterm REQUIRED)

    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2 + fontconfig${ColorReset}")

elseif(WIN32)
    # Windows: Use vcpkg for FreeType and fontconfig; FetchContent for libvterm

    # libvterm: Not in vcpkg, use FetchContent to build from source (Launchpad)
    include(FetchContent)
    FetchContent_Declare(
        libvterm
        URL "https://launchpad.net/libvterm/+download/libvterm-0.3.3.tar.gz"
        SOURCE_SUBDIR "."
    )

    FetchContent_MakeAvailable(libvterm)

    set(RENDER_FILE_LIBS freetype unofficial::fontconfig::fontconfig vterm)
    get_target_property(VTERM_INCLUDES vterm INTERFACE_INCLUDE_DIRECTORIES)
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDES})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2 + fontconfig${ColorReset}")

else()
    message(FATAL_ERROR "Unsupported platform for render-file backend")
endif()

# =============================================================================
# Backwards compatibility: Set GHOSTTY_* variables for legacy code
# =============================================================================

set(GHOSTTY_LIBS ${RENDER_FILE_LIBS})
set(GHOSTTY_INCLUDES ${RENDER_FILE_INCLUDES})
