# =============================================================================
# libvterm Dependency
# =============================================================================
# Cross-platform configuration for libvterm terminal emulation
#
# For musl builds: libvterm is not built from source (it depends on GTK ecosystem)
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

# Include FreeType2 and Fontconfig dependencies
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/FreeType2.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Fontconfig.cmake)

# Musl builds: libvterm is not built from source (no GTK ecosystem)
if(USE_MUSL)
    message(STATUS "${BoldYellow}⚠${ColorReset} libvterm: Not built for musl (GTK ecosystem excluded)")
    set(VTERM_LDFLAGS "")
    set(VTERM_LIBRARIES "")
    set(VTERM_INCLUDE_DIRS "")
    set(RENDER_FILE_LIBS "")
    set(RENDER_FILE_INCLUDES "")
    set(GHOSTTY_LIBS "")
    set(GHOSTTY_INCLUDES "")
    return()
endif()

# Non-musl builds: Use system package manager
if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM vterm REQUIRED)

    set(VTERM_LIBRARIES ${VTERM_LDFLAGS})
    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2 + fontconfig${ColorReset}")

elseif(APPLE)
    # macOS: Use system package managers (homebrew or macports)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM vterm REQUIRED)

    set(VTERM_LIBRARIES ${VTERM_LDFLAGS})
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

    set(VTERM_LIBRARIES vterm)
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
