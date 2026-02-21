# =============================================================================
# Render-to-file Dependencies Configuration
# =============================================================================
# Configure cross-platform renderer + FFmpeg encoder for --render-file feature
# Unix only (Linux + macOS); Windows builds with stubs

# ============================================================================= Bundled font setup (at configure time)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/fonts")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

set(MATRIX_FONT_URL "https://github.com/Rezmason/matrix/raw/master/assets/Matrix-Resurrected.ttf")
set(MATRIX_FONT_SRC "${CMAKE_BINARY_DIR}/fonts/Matrix-Resurrected.ttf")
set(MATRIX_FONT_GEN "${CMAKE_BINARY_DIR}/generated/matrix_resurrected_font.c")

# Download font at configure time
if(NOT EXISTS "${MATRIX_FONT_SRC}")
    message(STATUS "Downloading Matrix-Resurrected.ttf...")
    file(DOWNLOAD "${MATRIX_FONT_URL}" "${MATRIX_FONT_SRC}" SHOW_PROGRESS TLS_VERIFY ON)
endif()

# Convert to C array at configure time
if(NOT EXISTS "${MATRIX_FONT_GEN}" OR "${MATRIX_FONT_SRC}" IS_NEWER_THAN "${MATRIX_FONT_GEN}")
    message(STATUS "Embedding Matrix-Resurrected.ttf as C array...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${MATRIX_FONT_SRC}"
                "-DOUTPUT=${MATRIX_FONT_GEN}"
                "-DVAR_NAME=g_font_matrix_resurrected"
                "-P" "${CMAKE_SOURCE_DIR}/cmake/tools/bin2c.cmake"
        RESULT_VARIABLE BIN2C_RESULT
    )
    if(NOT BIN2C_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to convert font to C array")
    endif()
endif()

# ============================================================================= Platform-specific dependencies

if(WIN32)
    message(STATUS "Render-file: stubs only (Windows)")
    set(RENDER_FILE_LIBS "")
    set(RENDER_FILE_INCLUDES "")
elseif(APPLE)
    # macOS: ghostty (libghostty) + Metal
    find_library(GHOSTTY_LIB NAMES ghostty HINTS /usr/local/lib ~/src/github.com/ghostty-org/ghostty/zig-out/lib)
    find_path(GHOSTTY_INCLUDE NAMES ghostty.h HINTS /usr/local/include ~/src/github.com/ghostty-org/ghostty/zig-out/include)

    if(NOT GHOSTTY_LIB OR NOT GHOSTTY_INCLUDE)
        message(WARNING "Render-file: ghostty not found - macOS renderer will fail at runtime")
        message(STATUS "  To enable, build ghostty: cd ~/src/github.com/ghostty-org/ghostty && zig build -Doptimize=ReleaseFast")
        set(RENDER_FILE_LIBS "")
        set(RENDER_FILE_INCLUDES "")
    else()
        set(RENDER_FILE_LIBS ${GHOSTTY_LIB} "-framework Metal" "-framework Cocoa" "-framework CoreGraphics")
        set(RENDER_FILE_INCLUDES ${GHOSTTY_INCLUDE})
        message(STATUS "${BoldGreen}✓${ColorReset} Render-file (macOS): ghostty + Metal")
    endif()
else()
    # Linux: vterm + FreeType2 + fontconfig (REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM vterm REQUIRED)
    pkg_check_modules(FONTCONFIG fontconfig REQUIRED)

    set(RENDER_FILE_LIBS ${FREETYPE_LIBRARIES} ${VTERM_LDFLAGS} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${FREETYPE_INCLUDE_DIRS} ${VTERM_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})
    message(STATUS "${BoldGreen}✓${ColorReset} Render-file (Linux): libvterm + FreeType2 + fontconfig")
endif()
