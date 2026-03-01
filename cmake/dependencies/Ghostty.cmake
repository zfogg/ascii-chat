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

# =============================================================================
# Bundled font setup (build-time generation) - shared across all render backends
# =============================================================================

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/fonts")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/data/fonts")

set(MATRIX_FONT_URL "https://github.com/Rezmason/matrix/raw/master/assets/Matrix-Resurrected.ttf")
set(MATRIX_FONT_SRC "${CMAKE_BINARY_DIR}/fonts/Matrix-Resurrected.ttf")
set(MATRIX_FONT_GEN "${CMAKE_BINARY_DIR}/generated/data/fonts/matrix_resurrected.c")

# Download font at configure time (downloads are expensive, so cache them)
if(NOT EXISTS "${MATRIX_FONT_SRC}")
    message(STATUS "Downloading Matrix-Resurrected.ttf...")
    file(DOWNLOAD "${MATRIX_FONT_URL}" "${MATRIX_FONT_SRC}" SHOW_PROGRESS TLS_VERIFY ON)
endif()

# Download and extract a default monospace font (DejaVu Sans Mono) for fallback
# This ensures render-file works even if system fonts are unavailable
set(DEFAULT_FONT_TARBALL_URL "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2")
set(DEFAULT_FONT_TARBALL "${CMAKE_BINARY_DIR}/fonts/dejavu-fonts-ttf-2.37.tar.bz2")
set(DEFAULT_FONT_SRC "${CMAKE_BINARY_DIR}/fonts/DejaVuSansMono.ttf")
set(DEFAULT_FONT_GEN "${CMAKE_BINARY_DIR}/generated/data/fonts/default.c")

# Download and extract font tarball at configure time (caching, expensive download)
if(NOT EXISTS "${DEFAULT_FONT_SRC}")
    if(NOT EXISTS "${DEFAULT_FONT_TARBALL}")
        message(STATUS "Downloading DejaVu fonts tarball...")
        file(DOWNLOAD "${DEFAULT_FONT_TARBALL_URL}" "${DEFAULT_FONT_TARBALL}"
             SHOW_PROGRESS
             TLS_VERIFY ON
             STATUS DOWNLOAD_STATUS)
        list(GET DOWNLOAD_STATUS 0 DOWNLOAD_RESULT)
        list(GET DOWNLOAD_STATUS 1 DOWNLOAD_ERROR)
        if(NOT DOWNLOAD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download DejaVu fonts tarball: ${DOWNLOAD_ERROR}")
        endif()
    endif()

    message(STATUS "Extracting DejaVuSansMono.ttf from tarball...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xjf "${DEFAULT_FONT_TARBALL}" "dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono.ttf"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/fonts"
        RESULT_VARIABLE EXTRACT_RESULT
    )
    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract DejaVuSansMono.ttf from tarball")
    endif()

    # Move extracted file to the right place
    file(RENAME "${CMAKE_BINARY_DIR}/fonts/dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono.ttf" "${DEFAULT_FONT_SRC}")
endif()

# Create custom commands to generate C arrays at build time
# This ensures --clean-first regenerates the files automatically
add_custom_command(
    OUTPUT "${MATRIX_FONT_GEN}"
    COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${MATRIX_FONT_SRC}"
            "-DOUTPUT=${MATRIX_FONT_GEN}"
            "-DVAR_NAME=g_font_matrix_resurrected"
            "-P" "${CMAKE_SOURCE_DIR}/cmake/tools/bin2c.cmake"
    DEPENDS "${MATRIX_FONT_SRC}"
    COMMENT "Embedding Matrix-Resurrected.ttf as C array"
    VERBATIM
)

add_custom_command(
    OUTPUT "${DEFAULT_FONT_GEN}"
    COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${DEFAULT_FONT_SRC}"
            "-DOUTPUT=${DEFAULT_FONT_GEN}"
            "-DVAR_NAME=g_font_default"
            "-P" "${CMAKE_SOURCE_DIR}/cmake/tools/bin2c.cmake"
    DEPENDS "${DEFAULT_FONT_SRC}"
    COMMENT "Embedding DejaVuSansMono.ttf as C array"
    VERBATIM
)

# Create custom target to ensure fonts are built before main targets
add_custom_target(
    generate_fonts ALL
    DEPENDS "${MATRIX_FONT_GEN}" "${DEFAULT_FONT_GEN}"
)
