# =============================================================================
# libx265 Configuration - HEVC/H.265 Encoder Library (REQUIRED)
# =============================================================================
# libx265 is an HEVC encoder library required for FFmpeg h.265/HEVC video encoding.
# For musl builds, this is built from source in MuslDependencies.cmake
# For other platforms, it's found via system libraries or built from submodule.

if(USE_MUSL)
    if(LIBX265_FOUND)
        message(STATUS "${BoldGreen}✓${ColorReset} libx265 (musl): using musl-built static library")
        return()
    else()
        message(FATAL_ERROR "libx265 must be built in MuslDependencies.cmake for musl builds")
    endif()
endif()

# =============================================================================
# Windows: Use vcpkg-provided libx265
# =============================================================================
if(WIN32)
    find_package(x265 REQUIRED CONFIG)
    message(STATUS "${BoldGreen}✓${ColorReset} libx265 found (Windows vcpkg)")
    set(LIBX265_FOUND TRUE CACHE BOOL "libx265 found")
    set(LIBX265_LIBRARIES x265::x265)
    set(LIBX265_INCLUDE_DIRS "")
    return()
endif()

# =============================================================================
# Unix (non-musl): Try system libx265 first, build from submodule if not found
# =============================================================================
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBX265_PKG x265)
    if(LIBX265_PKG_FOUND)
        message(STATUS "${BoldGreen}✓${ColorReset} libx265 found (system pkg-config)")
        set(LIBX265_FOUND TRUE CACHE BOOL "libx265 found")
        set(LIBX265_INCLUDE_DIRS "${LIBX265_PKG_INCLUDE_DIRS}")
        set(LIBX265_LIBRARIES "${LIBX265_PKG_LIBRARIES}")
        return()
    endif()
endif()

# Try CMake find_package
find_package(x265 QUIET CONFIG)
if(x265_FOUND)
    message(STATUS "${BoldGreen}✓${ColorReset} libx265 found (system CMake)")
    set(LIBX265_FOUND TRUE CACHE BOOL "libx265 found")
    set(LIBX265_LIBRARIES x265::x265)
    set(LIBX265_INCLUDE_DIRS "")
    return()
endif()

# =============================================================================
# Build libx265 from submodule
# =============================================================================
message(STATUS "${BoldYellow}Building libx265 from submodule...${ColorReset}")
set(LIBX265_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/x265/source")
set(LIBX265_BINARY_DIR "${CMAKE_BINARY_DIR}/x265-build")

if(NOT EXISTS "${LIBX265_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "x265 submodule not found at ${LIBX265_SOURCE_DIR}")
endif()

# Disable CLI tools and extra features for minimal build
add_subdirectory("${LIBX265_SOURCE_DIR}" "${LIBX265_BINARY_DIR}" EXCLUDE_FROM_ALL)

set(LIBX265_FOUND TRUE CACHE BOOL "libx265 found")
set(LIBX265_LIBRARIES x265)
set(LIBX265_INCLUDE_DIRS "${LIBX265_SOURCE_DIR}")
set(LIBX265_LIBRARY_DIRS "${LIBX265_BINARY_DIR}")

message(STATUS "${BoldGreen}✓${ColorReset} libx265 will be built from submodule")
