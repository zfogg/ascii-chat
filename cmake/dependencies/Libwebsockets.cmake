# =============================================================================
# libwebsockets Library Configuration
# =============================================================================
# Finds and configures libwebsockets for WebSocket transport support
#
# libwebsockets enables browser clients to connect via WebSocket protocol,
# allowing WASM client builds and mixed TCP+WebSocket server support.
#
# Platform-specific dependency management:
#   - Windows: Uses vcpkg
#   - Linux/macOS: Uses pkg-config for system packages
#   - musl: Built from source (handled in MuslDependencies.cmake)
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - VCPKG_ROOT, VCPKG_TARGET_TRIPLET: (Windows only) vcpkg configuration
#
# Outputs (variables set by this file):
#   - LIBWEBSOCKETS_LIBRARIES: Libraries to link against
#   - LIBWEBSOCKETS_INCLUDE_DIRS: Include directories
#   - LIBWEBSOCKETS_FOUND: Whether libwebsockets was found
# =============================================================================

# Skip for musl builds - libwebsockets is configured in MuslDependencies.cmake
if(USE_MUSL)
    return()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

# On macOS, prefer Homebrew libwebsockets over system version if available
if(APPLE AND NOT USE_MUSL)
    if(EXISTS "/usr/local/opt/libwebsockets/lib/pkgconfig/libwebsockets.pc")
        set(ENV{PKG_CONFIG_PATH} "/usr/local/opt/libwebsockets/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    elseif(EXISTS "/opt/homebrew/opt/libwebsockets/lib/pkgconfig/libwebsockets.pc")
        set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/libwebsockets/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
endif()

find_dependency_library(
    NAME LIBWEBSOCKETS
    VCPKG_NAMES libwebsockets websockets
    HEADER libwebsockets.h
    PKG_CONFIG libwebsockets
    HOMEBREW_PKG libwebsockets
    STATIC_LIB_NAME libwebsockets.a
    REQUIRED
)

if(LIBWEBSOCKETS_FOUND)
    message(STATUS "libwebsockets found: ${LIBWEBSOCKETS_LIBRARIES}")
    message(STATUS "  WebSocket transport support: ENABLED")
    add_compile_definitions(HAVE_LIBWEBSOCKETS=1)
else()
    message(FATAL_ERROR "libwebsockets is required for WebSocket transport support. Install it via:")
    message(FATAL_ERROR "  - macOS: brew install libwebsockets")
    message(FATAL_ERROR "  - Ubuntu/Debian: sudo apt-get install libwebsockets-dev")
    message(FATAL_ERROR "  - Arch: sudo pacman -S libwebsockets")
    message(FATAL_ERROR "  - Windows: vcpkg install libwebsockets")
endif()
