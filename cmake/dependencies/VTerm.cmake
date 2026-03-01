# =============================================================================
# libvterm Dependency
# =============================================================================
# Cross-platform configuration for libvterm terminal emulation
#
# Outputs (variables set by this file):
#   - VTERM_LDFLAGS: libvterm libraries to link
#   - VTERM_INCLUDE_DIRS: libvterm include directories
# =============================================================================

# Include FreeType2 and Fontconfig dependencies
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/FreeType2.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Fontconfig.cmake)

if(UNIX AND NOT APPLE)
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

    # libvterm: Not in vcpkg, use FetchContent to build from source
    include(FetchContent)
    FetchContent_Declare(
        libvterm
        URL "https://github.com/neovim/libvterm/archive/refs/heads/master.zip"
        SOURCE_SUBDIR "."
    )

    FetchContent_MakeAvailable(libvterm)

    # Create VTERM_LDFLAGS and VTERM_INCLUDE_DIRS for consistency with other platforms
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
