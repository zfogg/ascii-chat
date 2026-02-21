# =============================================================================
# Ghostty Terminal Emulator Configuration
# =============================================================================
# Configures ghostty (libghostty) for render-to-file functionality on macOS
#
# Ghostty is used for:
#   - Cross-platform ASCII rendering on macOS (Metal backend)
#   - Character measurement and font handling
#
# Build strategy:
#   1. Check for system-installed ghostty (e.g., from package manager)
#   2. Build from submodule if system install not found (macOS only)
#   3. Cache built library for reuse across clean builds
#
# Outputs (variables set by this file):
#   - GHOSTTY_FOUND: Whether ghostty was found or built successfully
#   - GHOSTTY_LIBRARIES: Libraries to link against (ghostty_lib target)
#   - GHOSTTY_INCLUDE_DIRS: Include directories
# =============================================================================

include(ProcessorCount)
ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 1)
endif()

# Skip ghostty on non-macOS platforms (Windows uses stubs, Linux uses libvterm)
if(NOT APPLE)
    set(GHOSTTY_FOUND FALSE)
    set(GHOSTTY_LIBRARIES "")
    set(GHOSTTY_INCLUDE_DIRS "")
    return()
endif()

# Try to find ghostty (system install or package manager)
find_library(GHOSTTY_SYSTEM_LIB NAMES ghostty
             PATHS /usr/local/lib /usr/lib /opt/homebrew/lib
             NO_DEFAULT_PATH)
find_path(GHOSTTY_SYSTEM_INC NAMES ghostty.h
          PATHS /usr/local/include /usr/include /opt/homebrew/include
          NO_DEFAULT_PATH)

if(GHOSTTY_SYSTEM_LIB AND GHOSTTY_SYSTEM_INC)
    # Use system-installed ghostty
    add_library(ghostty_lib STATIC IMPORTED)
    set_target_properties(ghostty_lib PROPERTIES
        IMPORTED_LOCATION "${GHOSTTY_SYSTEM_LIB}"
    )
    target_include_directories(ghostty_lib INTERFACE "${GHOSTTY_SYSTEM_INC}")
    set(GHOSTTY_LIBRARIES ghostty_lib)
    set(GHOSTTY_INCLUDE_DIRS "${GHOSTTY_SYSTEM_INC}")
    set(GHOSTTY_FOUND TRUE)

    message(STATUS "Using system ${BoldGreen}ghostty${ColorReset} library: ${GHOSTTY_SYSTEM_LIB}")

# Fall back to building from submodule
elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ghostty")
    set(GHOSTTY_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ghostty")
    set(GHOSTTY_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/ghostty")
    set(GHOSTTY_LIB "${GHOSTTY_BUILD_DIR}/libghostty.a")

    file(MAKE_DIRECTORY "${GHOSTTY_BUILD_DIR}")

    # Only build if library doesn't exist in cache
    if(NOT EXISTS "${GHOSTTY_LIB}")
        message(STATUS "${BoldYellow}ghostty${ColorReset} library not found in cache, building from source...")

        # Check for zig compiler (required to build ghostty)
        find_program(ZIG_EXECUTABLE NAMES zig)
        if(NOT ZIG_EXECUTABLE)
            message(FATAL_ERROR "${BoldRed}zig compiler not found${ColorReset}. ghostty requires Zig to build.\n"
                              "Install from: https://ziglang.org/download/")
        endif()

        # Build ghostty using zig build
        set(GHOSTTY_LOG_FILE "${GHOSTTY_BUILD_DIR}/ghostty-build.log")
        execute_process(
            COMMAND "${ZIG_EXECUTABLE}" build -Doptimize=ReleaseFast
            WORKING_DIRECTORY "${GHOSTTY_SOURCE_DIR}"
            RESULT_VARIABLE GHOSTTY_BUILD_RESULT
            OUTPUT_FILE "${GHOSTTY_LOG_FILE}"
            ERROR_FILE "${GHOSTTY_LOG_FILE}"
        )

        if(NOT GHOSTTY_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "${BoldRed}ghostty build failed${ColorReset}. Check log: ${GHOSTTY_LOG_FILE}")
        endif()

        # Copy built library to cache (zig-cache/o/[hash]/libghostty.a -> cache)
        find_file(GHOSTTY_BUILT_LIB NAMES "libghostty.a"
                  PATHS "${GHOSTTY_SOURCE_DIR}/zig-cache"
                  HINTS "${GHOSTTY_SOURCE_DIR}/zig-out/lib"
                  NO_DEFAULT_PATH)

        if(GHOSTTY_BUILT_LIB)
            file(COPY_FILE "${GHOSTTY_BUILT_LIB}" "${GHOSTTY_LIB}")
            message(STATUS "  ${BoldGreen}ghostty${ColorReset} library built and cached successfully")
        else()
            message(FATAL_ERROR "${BoldRed}ghostty library not found${ColorReset} in build output")
        endif()

        # Create a dummy target so dependencies work
        add_custom_target(ghostty_build)
    else()
        message(STATUS "${BoldGreen}ghostty${ColorReset} library found in cache: ${BoldCyan}${GHOSTTY_LIB}${ColorReset}")
        add_custom_target(ghostty_build)
    endif()

    # Create an imported library that links to the built library
    add_library(ghostty_lib STATIC IMPORTED GLOBAL)
    set_target_properties(ghostty_lib PROPERTIES
        IMPORTED_LOCATION "${GHOSTTY_LIB}"
    )
    target_include_directories(ghostty_lib INTERFACE
        "${GHOSTTY_SOURCE_DIR}/zig-out/include"
    )
    add_dependencies(ghostty_lib ghostty_build)

    set(GHOSTTY_LIBRARIES ghostty_lib)
    set(GHOSTTY_INCLUDE_DIRS "${GHOSTTY_SOURCE_DIR}/zig-out/include")
    set(GHOSTTY_FOUND TRUE)

    message(STATUS "${BoldGreen}ghostty${ColorReset} configured: ${GHOSTTY_LIB}")

else()
    message(STATUS "${BoldYellow}ghostty submodule not found${ColorReset} - render-file macOS support will be limited")
    set(GHOSTTY_FOUND FALSE)
    set(GHOSTTY_LIBRARIES "")
    set(GHOSTTY_INCLUDE_DIRS "")
endif()

# =============================================================================
# Bundled font setup (at configure time) - shared across all render backends
# =============================================================================

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

# =============================================================================
# Render-to-file backend selection (platform-specific)
# =============================================================================

if(WIN32)
    message(STATUS "Render-file: stubs only (Windows)")
    set(RENDER_FILE_LIBS "")
    set(RENDER_FILE_INCLUDES "")
elseif(APPLE)
    if(GHOSTTY_FOUND)
        set(RENDER_FILE_LIBS ${GHOSTTY_LIBRARIES} "-framework Metal" "-framework Cocoa" "-framework CoreGraphics")
        set(RENDER_FILE_INCLUDES ${GHOSTTY_INCLUDE_DIRS})
        message(STATUS "${BoldGreen}✓${ColorReset} Render-file (macOS): ghostty + Metal")
    else()
        message(WARNING "Render-file: ghostty not found - macOS renderer will fail at runtime")
        set(RENDER_FILE_LIBS "")
        set(RENDER_FILE_INCLUDES "")
    endif()
else()
    # Linux: libvterm + FreeType2 + fontconfig (REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM vterm REQUIRED)
    pkg_check_modules(FONTCONFIG fontconfig REQUIRED)

    set(RENDER_FILE_LIBS ${FREETYPE_LIBRARIES} ${VTERM_LDFLAGS} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${FREETYPE_INCLUDE_DIRS} ${VTERM_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})
    message(STATUS "${BoldGreen}✓${ColorReset} Render-file (Linux): libvterm + FreeType2 + fontconfig")
endif()
