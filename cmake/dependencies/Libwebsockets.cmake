# =============================================================================
# libwebsockets Library Configuration
# =============================================================================
# Finds and configures libwebsockets for WebSocket transport support.
#
# libwebsockets enables browser clients to connect via WebSocket protocol,
# allowing WASM client builds and mixed TCP+WebSocket server support.
#
# Platform-specific dependency management:
#   - musl: Built from source (below)
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

# Skip if already configured (by MuslDependencies.cmake or other means)
# Check LIBWEBSOCKETS_FOUND first in case MuslDependencies.cmake already ran
if(LIBWEBSOCKETS_FOUND)
    if(TARGET websockets)
        message(STATUS "${BoldGreen}✓${ColorReset} libwebsockets (musl): using musl-built static library")
    endif()
    return()
endif()

# =============================================================================
# Musl build: Build from source
# =============================================================================
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}libwebsockets${ColorReset} from source (musl)...")

    include(ExternalProject)

    set(LWS_PREFIX "${MUSL_DEPS_DIR_STATIC}/libwebsockets")
    set(LWS_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libwebsockets-build")

    if(NOT EXISTS "${LWS_PREFIX}/lib/libwebsockets.a")
        message(STATUS "  libwebsockets library not found in cache, will build from source")

        # Pass musl-gcc path to the toolchain file via cache variable
        set(MUSL_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchains/MuslGcc.cmake")

        ExternalProject_Add(libwebsockets-musl
            URL https://github.com/warmcat/libwebsockets/archive/refs/tags/v4.5.2.tar.gz
            URL_HASH SHA256:04244efb7a6438c8c6bfc79b21214db5950f72c9cf57e980af57ca321aae87b2
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${LWS_BUILD_DIR}
            STAMP_DIR ${LWS_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            DEPENDS zlib-musl
            CMAKE_ARGS
                -DCMAKE_TOOLCHAIN_FILE=${MUSL_TOOLCHAIN_FILE}
                -DMUSL_GCC_PATH=${MUSL_GCC}
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=${LWS_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_C_FLAGS=-O2\ -fPIC\ -Wno-sign-conversion\ -Wno-error\ -isystem\ ${KERNEL_HEADERS_DIR}
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
                -DLWS_WITHOUT_EXTENSIONS=OFF
                -DLWS_WITH_ZLIB=ON
                -DLWS_WITH_BUNDLED_ZLIB=ON
                -DZLIB_INCLUDE_DIR=${ZLIB_INCLUDE_DIR}
                -DZLIB_LIBRARY=${ZLIB_LIBRARY}
                -DLWS_WITH_SOCKS5=OFF
            BUILD_BYPRODUCTS ${LWS_PREFIX}/lib/libwebsockets.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}libwebsockets${ColorReset} library found in cache: ${BoldMagenta}${LWS_PREFIX}/lib/libwebsockets.a${ColorReset}")
        add_custom_target(libwebsockets-musl)
    endif()

    set(LIBWEBSOCKETS_LIBRARIES "${LWS_PREFIX}/lib/libwebsockets.a")
    set(LIBWEBSOCKETS_INCLUDE_DIRS "${LWS_PREFIX}/include")
    set(LIBWEBSOCKETS_BUILD_TARGET libwebsockets-musl)
    add_compile_definitions(HAVE_LIBWEBSOCKETS=1)

    # Create placeholder directories so CMake validation doesn't fail at configure time
    file(MAKE_DIRECTORY "${LIBWEBSOCKETS_INCLUDE_DIRS}")

    # Create imported target for libwebsockets (musl build) to match Libwebsockets.cmake behavior
    add_library(websockets STATIC IMPORTED GLOBAL)
    set_target_properties(websockets PROPERTIES
        IMPORTED_LOCATION "${LIBWEBSOCKETS_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBWEBSOCKETS_INCLUDE_DIRS}"
    )
    add_dependencies(websockets libwebsockets-musl)

    set(LIBWEBSOCKETS_FOUND TRUE)
    return()
endif()

# =============================================================================
# Windows: use vcpkg via find_package
# =============================================================================
if(WIN32)
    # Try CMake config first, then fall back to pkg-config
    find_package(libwebsockets QUIET CONFIG)
    if(NOT libwebsockets_FOUND)
        include(FindPkgConfig)
        pkg_check_modules(libwebsockets REQUIRED libwebsockets)
        if(NOT TARGET libwebsockets::libwebsockets)
            add_library(libwebsockets::libwebsockets INTERFACE IMPORTED)
            target_include_directories(libwebsockets::libwebsockets INTERFACE ${libwebsockets_INCLUDE_DIRS})
            target_link_libraries(libwebsockets::libwebsockets INTERFACE ${libwebsockets_LIBRARIES})
        endif()
    endif()
    message(STATUS "${BoldGreen}✓${ColorReset} libwebsockets found (Windows vcpkg)")
    add_compile_definitions(HAVE_LIBWEBSOCKETS=1)

    # Create compatibility variables for the rest of the build
    get_target_property(LIBWEBSOCKETS_LIBRARIES websockets IMPORTED_LOCATION)
    if(NOT LIBWEBSOCKETS_LIBRARIES)
        set(LIBWEBSOCKETS_LIBRARIES websockets)
    endif()
    set(LIBWEBSOCKETS_FOUND TRUE)
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
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
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
            -DLWS_WITHOUT_EXTENSIONS=OFF
            -DLWS_WITH_ZLIB=ON
            -DLWS_WITH_BUNDLED_ZLIB=ON
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

# Create an imported target for consistency with find_package() targets
# Only create if not already created (by MuslDependencies.cmake or other means)
if(NOT TARGET websockets)
    add_library(websockets STATIC IMPORTED)
    # IMPORTED_LOCATION must be set (CMake requires it), but validation is deferred to link time
    set_property(TARGET websockets PROPERTY IMPORTED_LOCATION "${LIBWEBSOCKETS_LIBRARIES}")
    # Include directories - only add if they exist to avoid validation errors
    if(EXISTS "${LIBWEBSOCKETS_INCLUDE_DIRS}")
        target_include_directories(websockets INTERFACE "${LIBWEBSOCKETS_INCLUDE_DIRS}")
    endif()
    add_dependencies(websockets libwebsockets-native)
endif()

message(STATUS "${BoldGreen}✓${ColorReset} libwebsockets (native, extensions enabled): ${LIBWEBSOCKETS_LIBRARIES}")
