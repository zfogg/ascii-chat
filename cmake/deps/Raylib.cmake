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

# WASM/Emscripten builds: Build raylib from source with Emscripten toolchain
if(DEFINED EMSCRIPTEN)
    message(STATUS "Configuring ${BoldBlue}raylib${ColorReset} from source (WASM)...")

    include(ExternalProject)
    FetchContent_Declare(raylib-src
        URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz
        URL_HASH SHA256=98f049b9ea2a9c40a14e4e543eeea1a7ec3090ebdcd329c4ca2cf98bc9793482
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/raylib-src"
        UPDATE_DISCONNECTED ON
    )
    FetchContent_Populate(raylib-src)

    set(RAYLIB_PREFIX "${FETCHCONTENT_BASE_DIR}/raylib-wasm")
    set(RAYLIB_BUILD_DIR "${FETCHCONTENT_BASE_DIR}/raylib-wasm-build")

    if(NOT EXISTS "${RAYLIB_PREFIX}/lib/libraylib.a")
        message(STATUS "  raylib library not found in cache, will build from source")

        ExternalProject_Add(raylib-wasm
            SOURCE_DIR ${raylib-src_SOURCE_DIR}
            PREFIX ${RAYLIB_BUILD_DIR}
            STAMP_DIR ${RAYLIB_BUILD_DIR}/stamps
            BUILD_ALWAYS 0
            CMAKE_ARGS
                -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
                -DCMAKE_POLICY_VERSION_MINIMUM=3.18
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=${RAYLIB_PREFIX}
                -DBUILD_SHARED_LIBS=OFF
                -DPLATFORM=Web
                -DUSE_EXTERNAL_GLFW=OFF
                -DSUPPORT_GLFW=ON
                -DSUPPORT_OPENGL=OFF
                -DBUILD_EXAMPLES=OFF
                -DBUILD_GAMES=OFF
                "-DCMAKE_EXE_LINKER_FLAGS=-s USE_GLFW=3 -s WASM=1 -s ASYNCIFY"
            BUILD_COMMAND ${CMAKE_MAKE_PROGRAM}
            INSTALL_COMMAND ${CMAKE_MAKE_PROGRAM} install
            BUILD_BYPRODUCTS ${RAYLIB_PREFIX}/lib/libraylib.a
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}raylib${ColorReset} library found in cache: ${BoldMagenta}${RAYLIB_PREFIX}/lib/libraylib.a${ColorReset}")
        add_custom_target(raylib-wasm)
    endif()

    set(RAYLIB_FOUND TRUE)
    set(RAYLIB_LIBRARIES "${RAYLIB_PREFIX}/lib/libraylib.a")
    set(RAYLIB_INCLUDE_DIRS "${RAYLIB_PREFIX}/include")

    message(STATUS "${BoldGreen}✓${ColorReset} raylib (WASM): ${BoldCyan}libraylib${ColorReset}")
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
