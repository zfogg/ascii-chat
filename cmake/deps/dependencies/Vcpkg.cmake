# =============================================================================
# vcpkg Configuration (Cross-Platform)
# =============================================================================
# Configures vcpkg package manager for all platforms when USE_VCPKG is enabled
#
# Supports two modes:
#   1. Manifest mode: vcpkg.json in project root, dependencies installed to
#      ${CMAKE_BINARY_DIR}/vcpkg_installed/ automatically by toolchain
#   2. Classic mode: Global vcpkg installation, VCPKG_ROOT env var required
#
# Prerequisites (must be set before including this file):
#   - USE_VCPKG: Whether to use vcpkg
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - CMAKE_BUILD_TYPE: Build type (Debug, Release, Dev)
#   - VCPKG_ROOT: vcpkg installation path from environment (optional)
#
# Outputs (variables set by this file):
#   - VCPKG_ROOT: vcpkg root directory path
#   - VCPKG_TARGET_TRIPLET: Selected triplet (platform/arch-specific)
#   - VCPKG_LIB_PATH: Path to vcpkg libraries (release)
#   - VCPKG_DEBUG_LIB_PATH: Path to vcpkg libraries (debug)
#   - VCPKG_INCLUDE_PATH: Path to vcpkg headers
#   - VCPKG_MANIFEST_MODE: Whether manifest mode is active
#
# Platform-specific triplets:
#   Windows: x64/arm64-windows or x64/arm64-windows-static
#   Linux:   x64/arm64-linux
#   macOS:   x64/arm64-osx
# =============================================================================

if(NOT USE_VCPKG)
    return()
endif()

# =============================================================================
# Detect vcpkg mode and root directory
# =============================================================================

# Check if vcpkg toolchain was loaded (manifest mode or preset-based)
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    set(VCPKG_TOOLCHAIN_LOADED TRUE)
else()
    set(VCPKG_TOOLCHAIN_LOADED FALSE)
endif()

# Check for vcpkg.json manifest file
if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg.json")
    set(VCPKG_MANIFEST_MODE TRUE)
    message(STATUS "vcpkg: Using ${BoldGreen}manifest mode${ColorReset} (vcpkg.json found)")
else()
    set(VCPKG_MANIFEST_MODE FALSE)
    message(STATUS "vcpkg: Using ${BoldYellow}classic mode${ColorReset} (no vcpkg.json)")
endif()

# Setup vcpkg root path
if(DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
    message(STATUS "vcpkg: Root from environment: ${VCPKG_ROOT}")
elseif(VCPKG_TOOLCHAIN_LOADED)
    # Extract VCPKG_ROOT from toolchain file path
    get_filename_component(_vcpkg_toolchain_dir "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(VCPKG_ROOT "${_vcpkg_toolchain_dir}/../.." ABSOLUTE)
    message(STATUS "vcpkg: Root from toolchain: ${VCPKG_ROOT}")
else()
    message(WARNING "vcpkg: ${BoldRed}VCPKG_ROOT${ColorReset} not set and toolchain not loaded - dependency resolution may fail")
    return()
endif()

# =============================================================================
# Triplet selection
# =============================================================================

# Priority: VCPKG_TARGET_TRIPLET env var > auto-detect
if(DEFINED ENV{VCPKG_TARGET_TRIPLET})
    set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_TARGET_TRIPLET}")
    message(STATUS "vcpkg: Triplet from environment: ${BoldCyan}${VCPKG_TARGET_TRIPLET}${ColorReset}")
elseif(NOT DEFINED VCPKG_TARGET_TRIPLET)
    # Auto-detect architecture
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(_vcpkg_arch "arm64")
    else()
        set(_vcpkg_arch "x64")
    endif()

    # Determine platform suffix
    if(WIN32)
        set(_vcpkg_platform "windows")
    elseif(APPLE)
        set(_vcpkg_platform "osx")
    else()
        set(_vcpkg_platform "linux")
    endif()

    # Determine triplet based on platform and build type
    # Windows Release: static libraries to avoid DLL dependencies
    # Unix/macOS: always use dynamic libraries (static is default)
    if(WIN32 AND CMAKE_BUILD_TYPE MATCHES "Release")
        set(VCPKG_TARGET_TRIPLET "${_vcpkg_arch}-${_vcpkg_platform}-static")
        message(STATUS "vcpkg: Using ${BoldGreen}static triplet${ColorReset} for Windows Release: ${BoldCyan}${VCPKG_TARGET_TRIPLET}${ColorReset}")
    elseif(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_BUILD_TYPE MATCHES "Debug|Sanitize")
        # Windows Debug with Clang = ASan enabled
        # ASan requires release CRT, so use our custom triplet that builds deps with release CRT
        # See: https://learn.microsoft.com/en-us/cpp/sanitizers/asan-runtime
        # "ASan does not support linking with the debug CRT versions"
        set(VCPKG_TARGET_TRIPLET "${_vcpkg_arch}-${_vcpkg_platform}-asan")
        set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_SOURCE_DIR}/vcpkg-triplets")
        message(STATUS "vcpkg: Using ${BoldGreen}ASan triplet${ColorReset} (release CRT for ASan compatibility): ${BoldCyan}${VCPKG_TARGET_TRIPLET}${ColorReset}")
    else()
        set(VCPKG_TARGET_TRIPLET "${_vcpkg_arch}-${_vcpkg_platform}")
        if(WIN32)
            message(STATUS "vcpkg: Using ${BoldYellow}dynamic triplet${ColorReset} for Windows Debug/Dev: ${BoldCyan}${VCPKG_TARGET_TRIPLET}${ColorReset}")
        else()
            message(STATUS "vcpkg: Using triplet: ${BoldCyan}${VCPKG_TARGET_TRIPLET}${ColorReset}")
        endif()
    endif()
endif()

# =============================================================================
# Set library and include paths
# =============================================================================

if(VCPKG_MANIFEST_MODE AND VCPKG_TOOLCHAIN_LOADED)
    # Manifest mode: packages installed in build directory
    set(_vcpkg_installed_dir "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
else()
    # Classic mode: packages in global vcpkg installation
    set(_vcpkg_installed_dir "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}")
endif()

set(VCPKG_LIB_PATH "${_vcpkg_installed_dir}/lib")
set(VCPKG_DEBUG_LIB_PATH "${_vcpkg_installed_dir}/debug/lib")
set(VCPKG_INCLUDE_PATH "${_vcpkg_installed_dir}/include")

# Set CMake paths to use the selected triplet
set(CMAKE_PREFIX_PATH "${_vcpkg_installed_dir}" ${CMAKE_PREFIX_PATH})
include_directories("${VCPKG_INCLUDE_PATH}")
link_directories("${VCPKG_LIB_PATH}")

message(STATUS "vcpkg: Include path: ${VCPKG_INCLUDE_PATH}")
message(STATUS "vcpkg: Library path: ${VCPKG_LIB_PATH}")
