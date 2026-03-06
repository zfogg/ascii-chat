# =============================================================================
# miniupnpc Library Configuration
# =============================================================================
# Finds and configures miniupnpc for UPnP/NAT port mapping support
#
# miniupnpc enables automatic port forwarding on home routers, allowing
# ~70% of home users to use direct TCP connections without WebRTC.
#
# Platform-specific dependency management:
#   - Windows: Uses vcpkg
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Linux (musl): Dependencies built from source
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - VCPKG_ROOT, VCPKG_TARGET_TRIPLET: (Windows only) vcpkg configuration
#
# Outputs (variables set by this file):
#   - MINIUPNPC_LIBRARIES: Libraries to link against
#   - MINIUPNPC_INCLUDE_DIRS: Include directories
#   - MINIUPNPC_FOUND: Whether miniupnpc was found (not required, gracefully degrades)
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

# =============================================================================
# iOS: Build from source for iOS cross-compilation
# =============================================================================
if(PLATFORM_IOS)
    message(STATUS "Configuring ${BoldBlue}miniupnpc${ColorReset} from source (iOS cross-compile)...")

    set(MINIUPNPC_PREFIX "${IOS_DEPS_CACHE_DIR}/miniupnpc")
    set(MINIUPNPC_LIBRARY "${MINIUPNPC_PREFIX}/lib/libminiupnpc.a")
    set(MINIUPNPC_INCLUDE_DIRS "${MINIUPNPC_PREFIX}/include")

    if(NOT EXISTS "${MINIUPNPC_LIBRARY}")
        message(STATUS "  miniupnpc library not found in cache, will build from source")

        set(MINIUPNPC_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/miniupnpc-build")
        file(MAKE_DIRECTORY "${MINIUPNPC_BUILD_DIR}")

        # Get iOS SDK path
        if(BUILD_IOS_SIM)
            execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        else()
            execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path OUTPUT_VARIABLE IOS_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()

        message(STATUS "  Downloading miniupnpc 2.2.7...")
        file(DOWNLOAD
            "https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_2_7.tar.gz"
            "${MINIUPNPC_BUILD_DIR}/miniupnpc-2.2.7.tar.gz"
            TIMEOUT 30
            SHOW_PROGRESS
        )

        message(STATUS "  Extracting miniupnpc...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf miniupnpc-2.2.7.tar.gz
            WORKING_DIRECTORY "${MINIUPNPC_BUILD_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract miniupnpc")
        endif()

        message(STATUS "  Building miniupnpc for iOS...")
        execute_process(
            COMMAND bash -c "cd '${MINIUPNPC_BUILD_DIR}/miniupnp-miniupnpc_2_2_7/miniupnpc' && \
                    mkdir -p build && \
                    clang -Ibuild -DMINIUPNPC_SET_SOCKET_TIMEOUT -DMINIUPNPC_GET_SRC_ADDR -D_BSD_SOURCE -D_DEFAULT_SOURCE -Iinclude -D_DARWIN_C_SOURCE \
                    -std=gnu11 -O -Wall -W -Wstrict-prototypes -fno-common -fPIC \
                    -isysroot ${IOS_SDK_PATH} -arch arm64 -miphoneos-version-min=16.0 \
                    -c src/miniupnpc.c -o build/miniupnpc.o && \
                    clang -Ibuild -DMINIUPNPC_SET_SOCKET_TIMEOUT -DMINIUPNPC_GET_SRC_ADDR -D_BSD_SOURCE -D_DEFAULT_SOURCE -Iinclude -D_DARWIN_C_SOURCE \
                    -std=gnu11 -O -Wall -W -Wstrict-prototypes -fno-common -fPIC \
                    -isysroot ${IOS_SDK_PATH} -arch arm64 -miphoneos-version-min=16.0 \
                    -c src/upnpcommands.c -o build/upnpcommands.o && \
                    clang -Ibuild -DMINIUPNPC_SET_SOCKET_TIMEOUT -DMINIUPNPC_GET_SRC_ADDR -D_BSD_SOURCE -D_DEFAULT_SOURCE -Iinclude -D_DARWIN_C_SOURCE \
                    -std=gnu11 -O -Wall -W -Wstrict-prototypes -fno-common -fPIC \
                    -isysroot ${IOS_SDK_PATH} -arch arm64 -miphoneos-version-min=16.0 \
                    -c src/upnperrors.c -o build/upnperrors.o && \
                    ar rcs build/libminiupnpc.a build/miniupnpc.o build/upnpcommands.o build/upnperrors.o && \
                    mkdir -p '${MINIUPNPC_PREFIX}/lib' '${MINIUPNPC_PREFIX}/include' && \
                    cp build/libminiupnpc.a '${MINIUPNPC_PREFIX}/lib/' && \
                    cp include/miniupnpc.h '${MINIUPNPC_PREFIX}/include/' && \
                    cp include/upnpcommands.h '${MINIUPNPC_PREFIX}/include/' && \
                    cp include/upnperrors.h '${MINIUPNPC_PREFIX}/include/'"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "miniupnpc iOS build failed:\\n${BUILD_ERROR}")
        endif()
    else()
        message(STATUS "  ${BoldBlue}miniupnpc${ColorReset} library found in iOS cache: ${BoldMagenta}${MINIUPNPC_LIBRARY}${ColorReset}")
    endif()

    set(MINIUPNPC_FOUND TRUE)
    set(MINIUPNPC_LIBRARIES "${MINIUPNPC_LIBRARY}")

    # Create imported target
    if(NOT TARGET PkgConfig::MINIUPNPC)
        add_library(PkgConfig::MINIUPNPC STATIC IMPORTED GLOBAL)
        set_target_properties(PkgConfig::MINIUPNPC PROPERTIES
            IMPORTED_LOCATION "${MINIUPNPC_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MINIUPNPC_INCLUDE_DIRS}"
        )
    endif()

    message(STATUS "${BoldGreen}✓${ColorReset} miniupnpc (iOS): ${MINIUPNPC_LIBRARY}")
    return()
endif()

# On macOS, prefer Homebrew miniupnpc over system version if available
if(APPLE AND NOT USE_MUSL)
    if(HOMEBREW_PREFIX AND EXISTS "${HOMEBREW_PREFIX}/opt/miniupnpc/lib/pkgconfig/miniupnpc.pc")
        set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/opt/miniupnpc/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
endif()

# Note: miniupnpc is OPTIONAL. If not found, UPnP support is disabled but the build continues.
# This allows the code to gracefully degrade to WebRTC fallback for NAT traversal.
find_dependency_library(
    NAME MINIUPNPC
    VCPKG_NAMES miniupnpc
    HEADER miniupnpc/miniupnpc.h
    PKG_CONFIG miniupnpc
    HOMEBREW_PKG miniupnpc
    STATIC_LIB_NAME libminiupnpc.a
    OPTIONAL
)

# If miniupnpc is found, define HAVE_MINIUPNPC for the C code
if(MINIUPNPC_FOUND)
    message(STATUS "miniupnpc found: ${MINIUPNPC_LIBRARIES}")
    message(STATUS "  UPnP/NAT-PMP support: ENABLED")
    add_compile_definitions(HAVE_MINIUPNPC=1)

    # Detect the UPNP_GetValidIGD function signature (changed in API version 14)
    # Some distributions have mismatched header API version vs actual function signature,
    # so we must test at configure time rather than relying on MINIUPNPC_API_VERSION
    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES ${MINIUPNPC_INCLUDE_DIRS})

    # For check_c_source_compiles, we need actual library paths, not CMake imported targets
    # Extract the actual library location from the target if it's an imported target
    if(TARGET PkgConfig::MINIUPNPC)
        get_target_property(_miniupnpc_lib PkgConfig::MINIUPNPC INTERFACE_LINK_LIBRARIES)
        get_target_property(_miniupnpc_loc PkgConfig::MINIUPNPC IMPORTED_LOCATION)
        if(_miniupnpc_loc)
            set(CMAKE_REQUIRED_LIBRARIES ${_miniupnpc_loc})
        elseif(_miniupnpc_lib)
            set(CMAKE_REQUIRED_LIBRARIES ${_miniupnpc_lib})
        else()
            # Fallback: try to find the library manually
            find_library(_miniupnpc_lib_path NAMES miniupnpc)
            if(_miniupnpc_lib_path)
                set(CMAKE_REQUIRED_LIBRARIES ${_miniupnpc_lib_path})
            else()
                set(CMAKE_REQUIRED_LIBRARIES ${MINIUPNPC_LIBRARIES})
            endif()
        endif()
    else()
        set(CMAKE_REQUIRED_LIBRARIES ${MINIUPNPC_LIBRARIES})
    endif()

    check_c_source_compiles("
        #include <stddef.h>  // For size_t - miniupnpc headers need it but don't include it
        #include <miniupnpc/miniupnpc.h>
        #include <miniupnpc/upnpcommands.h>
        int main(void) {
            struct UPNPUrls urls;
            struct IGDdatas data;
            char addr[40];
            // Try the 7-argument version (API >= 14)
            int r = UPNP_GetValidIGD(0, &urls, &data, addr, sizeof(addr), 0, 0);
            return r;
        }
    " MINIUPNPC_HAS_7ARG_GETVALIDIGD)

    if(MINIUPNPC_HAS_7ARG_GETVALIDIGD)
        message(STATUS "  UPNP_GetValidIGD: 7-argument version (API >= 14)")
        add_compile_definitions(MINIUPNPC_GETVALIDIGD_7ARG=1)
    else()
        message(STATUS "  UPNP_GetValidIGD: 5-argument version (API < 14)")
    endif()

    # On macOS, also add libnatpmp include path and library (used alongside miniupnpc)
    if(APPLE)
        if(HOMEBREW_PREFIX AND EXISTS "${HOMEBREW_PREFIX}/opt/libnatpmp/include" AND EXISTS "${HOMEBREW_PREFIX}/opt/libnatpmp/lib/libnatpmp.a")
            # Don't use global include_directories() - it affects all targets including the defer tool
            # Instead, add includes only to targets that actually use libnatpmp
            set(NATPMP_INCLUDE_DIR "${HOMEBREW_PREFIX}/opt/libnatpmp/include")
            set(NATPMP_LIBRARY "${HOMEBREW_PREFIX}/opt/libnatpmp/lib/libnatpmp.a")
            message(STATUS "  NAT-PMP: ${HOMEBREW_PREFIX}/opt/libnatpmp")
        endif()
    endif()
else()
    message(STATUS "miniupnpc not found: UPnP/NAT-PMP will be disabled (graceful fallback to WebRTC)")
endif()
