# =============================================================================
# Raylib Game Framework Configuration
# =============================================================================
# Finds and configures raylib graphics library
#
# Raylib is used for:
#   - Cross-platform graphics rendering
#   - Window management and input handling
#   - Audio playback
#
# Build strategy:
#   - For WASM/Emscripten: Built from source with Emscripten toolchain
#   - For native (macOS/Linux): Uses system-installed raylib via pkg-config
#   - For Windows: Uses vcpkg or system package
#
# Prerequisites (must be set before including this file):
#   - FETCHCONTENT_BASE_DIR: Shared source cache directory
#
# Outputs (variables set by this file):
#   - RAYLIB_LIBRARIES: Libraries to link against
#   - RAYLIB_INCLUDE_DIRS: Include directories
#   - RAYLIB_LINK_DIR: Link directory (for WASM, used with -L flag)
#   - RAYLIB_FOUND: Whether raylib was found
# =============================================================================

include(FetchContent)

# WASM/Emscripten builds: Skip raylib - using xterm.js for rendering instead
if(DEFINED EMSCRIPTEN)
    message(STATUS "Configuring ${BoldBlue}raylib${ColorReset} (WASM)...")
    message(STATUS "${BoldYellow}⚠${ColorReset} raylib (WASM): Skipped - using xterm.js for terminal rendering")
    # raylib has complex graphics/windowing dependencies that don't work in WASM
    # The ASCII renderer in WASM generates a framebuffer; xterm.js handles display
    set(RAYLIB_LIBRARIES "")
    set(RAYLIB_INCLUDE_DIRS "")
    set(RAYLIB_FOUND TRUE)
    return()
endif()

# Native builds: Use system package or pkg-config
include(${CMAKE_CURRENT_LIST_DIR}/../utils/FindDependency.cmake)

find_dependency_library(
    NAME RAYLIB
    VCPKG_NAMES raylib
    HEADER raylib.h
    PKG_CONFIG raylib
    HOMEBREW_PKG raylib
    OPTIONAL
)
