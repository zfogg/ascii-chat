# =============================================================================
# Options Configuration Module
# =============================================================================
# This module configures build options like mimalloc and musl
#
# Prerequisites:
#   - USE_MUSL must be set (declared before project())
#   - CMAKE_BUILD_TYPE must be set
#
# Outputs:
#   - USE_MIMALLOC option configured based on build type and musl
#   - USE_CCACHE disabled for musl builds
# =============================================================================

# Option to use mimalloc (must be defined early so build type flags can check it)
# =============================================================================
# mimalloc Configuration
# =============================================================================
# mimalloc provides significant performance benefits for all Release builds
#
# Default behavior by platform and build type:
#   Linux Release/RelWithDebInfo + musl:
#     - ON (musl + mimalloc = ~85% faster than glibc, MI_OVERRIDE=OFF)
#   macOS/Windows Release/RelWithDebInfo:
#     - ON (system libc + mimalloc with MI_OVERRIDE=ON for global replacement)
#   Debug/Dev builds:
#     - OFF (use system allocator for better debugging)
#
# Performance results:
#   - glibc + mimalloc:  ~60 FPS
#   - musl + mimalloc:   ~100 FPS (recommended for Linux)
#   - musl only:         ~55 FPS
#
if(USE_MUSL)
    # musl + mimalloc with MI_OVERRIDE=OFF (explicit mi_malloc/mi_free calls)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        option(USE_MIMALLOC "Use mimalloc with musl for optimal performance (MI_OVERRIDE=OFF)" ON)
        if(USE_MIMALLOC)
            message(STATUS "mimalloc enabled for musl build (MI_OVERRIDE=OFF, MI_LOCAL_DYNAMIC_TLS=ON)")
        endif()
    else()
        option(USE_MIMALLOC "Use mimalloc with musl (disabled for Debug/Dev)" OFF)
        if(NOT USE_MIMALLOC)
            message(STATUS "mimalloc disabled for musl Debug/Dev build - using musl's allocator")
        endif()
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
    # Debug/Dev: disable mimalloc for better debugging experience
    option(USE_MIMALLOC "Use mimalloc high-performance allocator (disabled for Debug/Dev)" OFF)
else()
    # Release/RelWithDebInfo on non-musl (macOS, Windows, glibc Linux)
    option(USE_MIMALLOC "Use mimalloc high-performance allocator (MI_OVERRIDE=ON)" ON)
endif()

# musl builds configuration
if(USE_MUSL)
    set(USE_CCACHE OFF CACHE BOOL "Disable ccache when using musl" FORCE)
    # Note: Sanitizers are automatically disabled for musl+mimalloc builds
    # in configure_sanitizers() - no need to change build type
endif()

