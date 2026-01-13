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

# On macOS, prefer Homebrew miniupnpc over system version if available
if(APPLE AND NOT USE_MUSL)
    if(EXISTS "/usr/local/opt/miniupnpc/lib/pkgconfig/miniupnpc.pc")
        set(ENV{PKG_CONFIG_PATH} "/usr/local/opt/miniupnpc/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    elseif(EXISTS "/opt/homebrew/opt/miniupnpc/lib/pkgconfig/miniupnpc.pc")
        set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/miniupnpc/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
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
    set(CMAKE_REQUIRED_LIBRARIES ${MINIUPNPC_LIBRARIES})
    check_c_source_compiles("
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
        if(EXISTS "/usr/local/opt/libnatpmp/include" AND EXISTS "/usr/local/opt/libnatpmp/lib/libnatpmp.a")
            include_directories("/usr/local/opt/libnatpmp/include")
            set(NATPMP_LIBRARY "/usr/local/opt/libnatpmp/lib/libnatpmp.a")
            message(STATUS "  NAT-PMP: /usr/local/opt/libnatpmp")
        elseif(EXISTS "/opt/homebrew/opt/libnatpmp/include" AND EXISTS "/opt/homebrew/opt/libnatpmp/lib/libnatpmp.a")
            include_directories("/opt/homebrew/opt/libnatpmp/include")
            set(NATPMP_LIBRARY "/opt/homebrew/opt/libnatpmp/lib/libnatpmp.a")
            message(STATUS "  NAT-PMP: /opt/homebrew/opt/libnatpmp")
        endif()
    endif()
else()
    message(STATUS "miniupnpc not found: UPnP/NAT-PMP will be disabled (graceful fallback to WebRTC)")
endif()
