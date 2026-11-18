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
#   - _asciichat_miniupnpc_link: Libraries to link against
#   - _asciichat_miniupnpc_include: Include directories
#   - _asciichat_miniupnpc_found: Whether miniupnpc was found (required for native builds)
# =============================================================================

include(${CMAKE_CURRENT_LIST_DIR}/../utils/FindDependency.cmake)


include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Browser transports cannot perform native UPnP/NAT-PMP port mapping.
    # Do not discover a host miniupnpc package while configuring WASM.
    set(_asciichat_miniupnpc_found FALSE)
    set(_asciichat_miniupnpc_link "")
    set(_asciichat_miniupnpc_include "")
    message(STATUS "miniupnpc skipped for WebAssembly (browser networking has no UPnP API)")
    return()
endif()

if(PLATFORM_IOS OR USE_MUSL OR ASCIICHAT_BUILD_DEPS_FROM_SOURCE)
    function(asciichat_build_miniupnpc)
        asciichat_dependency_defaults()
        set(UPNPC_BUILD_SHARED ${BUILD_SHARED_LIBS})
        set(UPNPC_BUILD_STATIC ON)
        if(BUILD_SHARED_LIBS)
            set(UPNPC_BUILD_STATIC OFF)
        endif()
        set(UPNPC_BUILD_TESTS OFF)
        set(UPNPC_BUILD_SAMPLE OFF)
        asciichat_fetchcontent_declare(asciichat_miniupnpc
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/miniupnpc"
            EXCLUDE_FROM_ALL
            URL https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_2_7.tar.gz
            SOURCE_SUBDIR miniupnpc DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(asciichat_miniupnpc)
        set(_asciichat_miniupnpc_include "${asciichat_miniupnpc_SOURCE_DIR}/miniupnpc/include" PARENT_SCOPE)
    endfunction()
    asciichat_build_miniupnpc()
    set(_asciichat_miniupnpc_found TRUE)
    set(_asciichat_miniupnpc_link miniupnpc::miniupnpc)
    if(NOT TARGET asciichat-miniupnpc)
        add_library(asciichat-miniupnpc INTERFACE)
        target_link_libraries(asciichat-miniupnpc INTERFACE miniupnpc::miniupnpc)
        target_include_directories(asciichat-miniupnpc INTERFACE "${asciichat_miniupnpc_SOURCE_DIR}/miniupnpc/include")
    endif()
    if(TARGET asciichat-project-options)
        target_compile_definitions(asciichat-project-options INTERFACE HAVE_MINIUPNPC=1)
    endif()
    return()
endif()

# On macOS, prefer Homebrew miniupnpc over system version if available
if(APPLE AND NOT USE_MUSL)
    if(HOMEBREW_PREFIX AND EXISTS "${HOMEBREW_PREFIX}/opt/miniupnpc/lib/pkgconfig/miniupnpc.pc")
        set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/opt/miniupnpc/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
endif()

# miniupnpc is required for every native build. NAT traversal fallback is a
# runtime behavior, not permission to ship a binary without the dependency.
find_dependency_library(
    NAME MINIUPNPC
    VCPKG_NAMES miniupnpc
    HEADER miniupnpc/miniupnpc.h
    PKG_CONFIG miniupnpc
    HOMEBREW_PKG miniupnpc
    STATIC_LIB_NAME libminiupnpc.a
)

# If miniupnpc is found, define HAVE_MINIUPNPC for the C code
if(_asciichat_miniupnpc_found)
    unset(MINIUPNPC_HEADER_INCLUDE_DIR CACHE)
    foreach(_miniupnpc_include_candidate IN ITEMS
        ${_asciichat_miniupnpc_include} ${MINIUPNPC_INCLUDE_DIR}
        /usr/include /usr/include/miniupnpc
        /usr/local/include /usr/local/include/miniupnpc)
        if(EXISTS "${_miniupnpc_include_candidate}/miniupnpc.h")
            set(MINIUPNPC_HEADER_INCLUDE_DIR "${_miniupnpc_include_candidate}")
            break()
        elseif(EXISTS "${_miniupnpc_include_candidate}/miniupnpc/miniupnpc.h")
            set(MINIUPNPC_HEADER_INCLUDE_DIR "${_miniupnpc_include_candidate}/miniupnpc")
            break()
        endif()
    endforeach()
    if(NOT MINIUPNPC_HEADER_INCLUDE_DIR)
        find_path(MINIUPNPC_HEADER_INCLUDE_DIR NAMES miniupnpc.h
            HINTS ${_asciichat_miniupnpc_include} PATH_SUFFIXES miniupnpc REQUIRED)
    endif()
    set(_asciichat_miniupnpc_include "${MINIUPNPC_HEADER_INCLUDE_DIR}")
    message(STATUS "miniupnpc found: ${_asciichat_miniupnpc_link}")
    message(STATUS "  UPnP/NAT-PMP support: ENABLED")
    if(TARGET asciichat-project-options)
        target_compile_definitions(asciichat-project-options INTERFACE HAVE_MINIUPNPC=1)
    endif()

    # On Windows static builds, tell miniupnpc headers not to use __declspec(dllimport)
    if(WIN32 AND VCPKG_TARGET_TRIPLET MATCHES "static")
        if(TARGET asciichat-project-options)
            target_compile_definitions(asciichat-project-options INTERFACE MINIUPNP_STATICLIB=1)
        endif()
    endif()

    # Detect the UPNP_GetValidIGD function signature (changed in API version 14)
    # Some distributions have mismatched header API version vs actual function signature,
    # so we must test at configure time rather than relying on MINIUPNPC_API_VERSION
    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES ${_asciichat_miniupnpc_include})

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
                set(CMAKE_REQUIRED_LIBRARIES ${_asciichat_miniupnpc_link})
            endif()
        endif()
    else()
        set(CMAKE_REQUIRED_LIBRARIES ${_asciichat_miniupnpc_link})
    endif()

    # The result depends on the selected MiniUPnP headers.  Do not reuse a
    # cached probe result when switching toolchains or platform dependency
    # roots in an existing build directory.
    unset(MINIUPNPC_HAS_7ARG_GETVALIDIGD CACHE)
    check_c_source_compiles("
        #include <stddef.h>  // For size_t - miniupnpc headers need it but don't include it
        #include <miniupnpc.h>
        #include <upnpcommands.h>
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
        if(TARGET asciichat-project-options)
            target_compile_definitions(asciichat-project-options INTERFACE MINIUPNPC_GETVALIDIGD_7ARG=1)
        endif()
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
endif()

if(NOT _asciichat_miniupnpc_found)
    message(FATAL_ERROR "miniupnpc is required for native builds")
endif()
if(NOT TARGET asciichat-miniupnpc)
    add_library(asciichat-miniupnpc INTERFACE)
    if(TARGET PkgConfig::MINIUPNPC)
        target_link_libraries(asciichat-miniupnpc INTERFACE PkgConfig::MINIUPNPC)
    else()
        target_link_libraries(asciichat-miniupnpc INTERFACE ${_asciichat_miniupnpc_link})
    endif()
    if(_asciichat_miniupnpc_include)
        target_include_directories(asciichat-miniupnpc INTERFACE "${_asciichat_miniupnpc_include}")
    endif()
    if(EXISTS "/usr/include/miniupnpc/miniupnpc.h")
        target_include_directories(asciichat-miniupnpc INTERFACE "/usr/include/miniupnpc")
    endif()
endif()
if(TARGET asciichat-miniupnpc AND MINIUPNPC_HEADER_INCLUDE_DIR)
    target_include_directories(asciichat-miniupnpc INTERFACE
        "${MINIUPNPC_HEADER_INCLUDE_DIR}")
endif()
