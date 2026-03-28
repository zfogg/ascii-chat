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

# WASM/Emscripten builds: Build from source
if(DEFINED EMSCRIPTEN)
    message(STATUS "Configuring ${BoldBlue}raylib${ColorReset} from source (WASM)...")

    FetchContent_Declare(raylib-wasm
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/raylib-src"
        UPDATE_DISCONNECTED ON
        CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
                   -DPLATFORM=Web
                   -DGRAPHICS=GRAPHICS_API_OPENGL_ES2
                   -DBUILD_EXAMPLES=OFF
                   -DBUILD_GAMES=OFF
    )

    FetchContent_MakeAvailable(raylib-wasm)
    set(RAYLIB_LIBRARIES raylib)
    set(RAYLIB_INCLUDE_DIRS "${raylib-wasm_SOURCE_DIR}/src")
    set(RAYLIB_LINK_DIR "${raylib-wasm_BINARY_DIR}")
    set(RAYLIB_FOUND TRUE)

    message(STATUS "${BoldGreen}✓${ColorReset} raylib (WASM): ${BoldCyan}raylib${ColorReset}")
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
