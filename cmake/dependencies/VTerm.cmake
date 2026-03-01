# =============================================================================
# Render-to-File Dependencies: libvterm + FreeType2 + fontconfig
# =============================================================================
# Unified cross-platform dependencies for render-to-file functionality
#
# All platforms use the same backend:
#   - libvterm: Terminal emulation
#   - FreeType2: Glyph rasterization
#   - fontconfig: Font resolution (Linux/macOS/Windows)
#
# Outputs (variables set by this file):
#   - RENDER_FILE_LIBS: Libraries to link against (vterm, FreeType, fontconfig)
#   - RENDER_FILE_INCLUDES: Include directories for render-file
#
# Backwards compatibility:
#   - GHOSTTY_LIBS: Alias for RENDER_FILE_LIBS
#   - GHOSTTY_INCLUDES: Alias for RENDER_FILE_INCLUDES
# =============================================================================

# =============================================================================
# Find render-file dependencies: libvterm, FreeType2, fontconfig
# =============================================================================

if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)

    # libvterm
    pkg_check_modules(VTERM vterm REQUIRED)

    # FreeType2
    find_package(Freetype REQUIRED)

    # fontconfig
    pkg_check_modules(FONTCONFIG fontconfig REQUIRED)

    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2 + fontconfig${ColorReset}")
    message(STATUS "  libvterm: ${VTERM_LDFLAGS}")
    message(STATUS "  FreeType2: ${FREETYPE_LIBRARIES}")
    message(STATUS "  fontconfig: ${FONTCONFIG_LDFLAGS}")

elseif(APPLE)
    # macOS: Use system package managers (homebrew or macports)
    find_package(PkgConfig REQUIRED)

    # libvterm
    pkg_check_modules(VTERM vterm REQUIRED)

    # FreeType2
    find_package(Freetype REQUIRED)

    # fontconfig
    pkg_check_modules(FONTCONFIG fontconfig REQUIRED)

    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2 + fontconfig${ColorReset}")
    message(STATUS "  libvterm: ${VTERM_LDFLAGS}")
    message(STATUS "  FreeType2: ${FREETYPE_LIBRARIES}")
    message(STATUS "  fontconfig: ${FONTCONFIG_LDFLAGS}")

elseif(WIN32)
    # Windows: Use vcpkg for FreeType and fontconfig; FetchContent for libvterm

    # FreeType2
    find_package(freetype CONFIG REQUIRED)

    # fontconfig
    find_package(unofficial-fontconfig CONFIG REQUIRED)

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
    message(STATUS "  libvterm: built from source")
    message(STATUS "  FreeType2: vcpkg")
    message(STATUS "  fontconfig: vcpkg")

else()
    message(FATAL_ERROR "Unsupported platform for render-file backend")
endif()

# =============================================================================
# Backwards compatibility: Set GHOSTTY_* variables for legacy code
# =============================================================================

set(GHOSTTY_LIBS ${RENDER_FILE_LIBS})
set(GHOSTTY_INCLUDES ${RENDER_FILE_INCLUDES})
