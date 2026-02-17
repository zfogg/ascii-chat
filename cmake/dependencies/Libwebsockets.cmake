# =============================================================================
# libwebsockets Library Configuration
# =============================================================================
# Finds and configures libwebsockets for WebSocket transport support.
#
# libwebsockets enables browser clients to connect via WebSocket protocol,
# allowing WASM client builds and mixed TCP+WebSocket server support.
#
# Platform-specific dependency management:
#   - musl: Built from source in MuslDependencies.cmake (not here)
#   - Linux/macOS native: Built from source (ExternalProject_Add) with
#     permessage-deflate extension support. System packages often ship with
#     LWS_WITHOUT_EXTENSIONS=ON which disables RFC 7692 compression.
#   - Windows: Uses vcpkg
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - VCPKG_ROOT, VCPKG_TARGET_TRIPLET: (Windows only) vcpkg configuration
#   - ASCIICHAT_DEPS_CACHE_DIR: Dependency cache directory (non-Windows)
#
# Outputs (variables set by this file):
#   - LIBWEBSOCKETS_LIBRARIES: Libraries to link against
#   - LIBWEBSOCKETS_INCLUDE_DIRS: Include directories
#   - LIBWEBSOCKETS_FOUND: Whether libwebsockets was found
# =============================================================================

# Skip for musl builds - libwebsockets is built from source in MuslDependencies.cmake
if(USE_MUSL)
    if(LIBWEBSOCKETS_FOUND)
        message(STATUS "${BoldGreen}✓${ColorReset} libwebsockets (musl): using musl-built static library")
    endif()
    return()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

# =============================================================================
# Windows: use vcpkg
# =============================================================================
if(WIN32)
    find_dependency_library(
        NAME LIBWEBSOCKETS
        VCPKG_NAMES libwebsockets websockets
        HEADER libwebsockets.h
        PKG_CONFIG libwebsockets
        STATIC_LIB_NAME libwebsockets.a
        REQUIRED
    )
    if(LIBWEBSOCKETS_FOUND)
        message(STATUS "libwebsockets (vcpkg): ${LIBWEBSOCKETS_LIBRARIES}")
        add_compile_definitions(HAVE_LIBWEBSOCKETS=1)
    else()
        message(FATAL_ERROR "libwebsockets not found via vcpkg. Run: vcpkg install libwebsockets")
    endif()
    return()
endif()

# =============================================================================
# Linux / macOS: build from source with extensions enabled
#
# System packages (pacman, apt, brew) typically ship with LWS_WITHOUT_EXTENSIONS=ON,
# which disables permessage-deflate (RFC 7692). We build from source so we can
# enable extensions and zlib compression support.
# =============================================================================
include(ExternalProject)

set(LWS_NATIVE_PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/libwebsockets")
set(LWS_NATIVE_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/libwebsockets-build")

if(NOT EXISTS "${LWS_NATIVE_PREFIX}/lib/libwebsockets.a")
    message(STATUS "  libwebsockets not in cache, building from source with extensions enabled...")

    ExternalProject_Add(libwebsockets-native
        URL https://github.com/warmcat/libwebsockets/archive/refs/tags/v4.5.2.tar.gz
        URL_HASH SHA256=04244efb7a6438c8c6bfc79b21214db5950f72c9cf57e980af57ca321aae87b2
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${LWS_NATIVE_BUILD_DIR}
        STAMP_DIR ${LWS_NATIVE_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${LWS_NATIVE_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DLWS_WITH_SHARED=OFF
            -DLWS_WITH_STATIC=ON
            -DLWS_WITHOUT_TESTAPPS=ON
            -DLWS_WITHOUT_TEST_SERVER=ON
            -DLWS_WITHOUT_TEST_SERVER_EXTPOLL=ON
            -DLWS_WITHOUT_TEST_PING=ON
            -DLWS_WITHOUT_TEST_CLIENT=ON
            -DLWS_WITH_SSL=OFF
            -DLWS_WITH_LIBEV=OFF
            -DLWS_WITH_LIBUV=OFF
            -DLWS_WITH_LIBEVENT=OFF
            -DLWS_WITH_GLIB=OFF
            -DLWS_WITH_SYSTEMD=OFF
            -DLWS_WITH_LIBCAP=OFF
            -DLWS_WITH_JOSE=OFF
            -DLWS_WITH_GENCRYPTO=OFF
            -DLWS_IPV6=ON
            -DLWS_UNIX_SOCK=ON
            -DLWS_WITHOUT_DAEMONIZE=ON
            -DLWS_WITHOUT_EXTENSIONS=ON
            -DLWS_WITH_ZLIB=OFF
            -DLWS_WITH_BUNDLED_ZLIB=OFF
            -DLWS_WITH_SOCKS5=OFF
        BUILD_BYPRODUCTS ${LWS_NATIVE_PREFIX}/lib/libwebsockets.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "  ${BoldBlue}libwebsockets${ColorReset} (native) found in cache: ${BoldMagenta}${LWS_NATIVE_PREFIX}/lib/libwebsockets.a${ColorReset}")
    add_custom_target(libwebsockets-native)
endif()

set(LIBWEBSOCKETS_FOUND TRUE)
set(LIBWEBSOCKETS_LIBRARIES "${LWS_NATIVE_PREFIX}/lib/libwebsockets.a")
set(LIBWEBSOCKETS_INCLUDE_DIRS "${LWS_NATIVE_PREFIX}/include")
set(LIBWEBSOCKETS_BUILD_TARGET libwebsockets-native)
add_compile_definitions(HAVE_LIBWEBSOCKETS=1)

message(STATUS "${BoldGreen}✓${ColorReset} libwebsockets (native, extensions enabled): ${LIBWEBSOCKETS_LIBRARIES}")
