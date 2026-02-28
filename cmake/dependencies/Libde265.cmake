# =============================================================================
# libde265 Configuration - HEVC/H.265 Decoder Library (REQUIRED)
# =============================================================================
# libde265 is a HEVC decoder library required for FFmpeg h.265/HEVC video decoding.
# For musl builds, this is built from source in MuslDependencies.cmake
# For other platforms, it's found via system libraries or built from submodule.

if(USE_MUSL)
    if(LIBDE265_FOUND)
        message(STATUS "${BoldGreen}✓${ColorReset} libde265 (musl): using musl-built static library")
        return()
    else()
        message(FATAL_ERROR "libde265 must be built in MuslDependencies.cmake for musl builds")
    endif()
endif()

# =============================================================================
# Windows: Use vcpkg-provided libde265
# =============================================================================
if(WIN32)
    find_package(libde265 REQUIRED CONFIG)
    message(STATUS "${BoldGreen}✓${ColorReset} libde265 found (Windows vcpkg)")
    set(LIBDE265_FOUND TRUE CACHE BOOL "libde265 found")
    set(LIBDE265_LIBRARIES libde265::libde265)
    set(LIBDE265_INCLUDE_DIRS "")
    return()
endif()

# =============================================================================
# Unix (non-musl): Try system libde265 first, build from submodule if not found
# =============================================================================
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBDE265_PKG libde265)
    if(LIBDE265_PKG_FOUND)
        message(STATUS "${BoldGreen}✓${ColorReset} libde265 found (system pkg-config)")
        set(LIBDE265_FOUND TRUE CACHE BOOL "libde265 found")
        set(LIBDE265_INCLUDE_DIRS "${LIBDE265_PKG_INCLUDE_DIRS}")
        set(LIBDE265_LIBRARIES "${LIBDE265_PKG_LIBRARIES}")
        return()
    endif()
endif()

# Try CMake find_package
find_package(libde265 QUIET CONFIG)
if(libde265_FOUND)
    message(STATUS "${BoldGreen}✓${ColorReset} libde265 found (system CMake)")
    set(LIBDE265_FOUND TRUE CACHE BOOL "libde265 found")
    set(LIBDE265_LIBRARIES libde265::libde265)
    set(LIBDE265_INCLUDE_DIRS "")
    return()
endif()

# =============================================================================
# Build libde265 from submodule
# =============================================================================
message(STATUS "${BoldYellow}Building libde265 from submodule...${ColorReset}")
set(LIBDE265_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libde265")
set(LIBDE265_BINARY_DIR "${CMAKE_BINARY_DIR}/libde265-build")

if(NOT EXISTS "${LIBDE265_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "libde265 submodule not found at ${LIBDE265_SOURCE_DIR}")
endif()

add_subdirectory("${LIBDE265_SOURCE_DIR}" "${LIBDE265_BINARY_DIR}" EXCLUDE_FROM_ALL)

set(LIBDE265_FOUND TRUE CACHE BOOL "libde265 found")
set(LIBDE265_LIBRARIES de265)
set(LIBDE265_INCLUDE_DIRS "${LIBDE265_SOURCE_DIR}/libde265")
set(LIBDE265_LIBRARY_DIRS "${LIBDE265_BINARY_DIR}")

message(STATUS "${BoldGreen}✓${ColorReset} libde265 will be built from submodule")
