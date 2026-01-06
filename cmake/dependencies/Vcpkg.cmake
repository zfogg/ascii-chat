# =============================================================================
# vcpkg Configuration (Cross-Platform)
# =============================================================================
# Configures vcpkg package manager for all platforms when USE_VCPKG is enabled
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
#
# Platform-specific triplets:
#   Windows: x64/arm64-windows or x64/arm64-windows-static
#   Linux:   x64/arm64-linux
#   macOS:   x64/arm64-osx
# =============================================================================

if(USE_VCPKG)
    # Setup vcpkg paths if available
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT $ENV{VCPKG_ROOT})
        message(STATUS "Using ${BoldGreen}vcpkg${ColorReset} from: ${VCPKG_ROOT}")
    else()
        message(WARNING "${BoldRed}VCPKG_ROOT${ColorReset} environment variable not set - dependency resolution may fail")
        return()
    endif()

    # Determine architecture for triplet selection
    # Priority: VCPKG_TARGET_TRIPLET env var > CMAKE_HOST_SYSTEM_PROCESSOR > default x64
    if(DEFINED ENV{VCPKG_TARGET_TRIPLET})
        # Use triplet from environment (useful for CI or cross-compilation)
        set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_TARGET_TRIPLET}")
        message(STATUS "Using vcpkg triplet from environment: ${BoldCyan}${VCPKG_TARGET_TRIPLET}${ColorReset}")
    else()
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
            message(STATUS "Using ${BoldGreen}static libraries${ColorReset} for Windows Release build (triplet: ${BoldCyan}${VCPKG_TARGET_TRIPLET}${ColorReset})")
        else()
            set(VCPKG_TARGET_TRIPLET "${_vcpkg_arch}-${_vcpkg_platform}")
            if(WIN32)
                message(STATUS "Using ${BoldYellow}dynamic libraries${ColorReset} for Windows Debug/Dev build (triplet: ${BoldCyan}${VCPKG_TARGET_TRIPLET}${ColorReset})")
            else()
                message(STATUS "Using vcpkg triplet: ${BoldCyan}${VCPKG_TARGET_TRIPLET}${ColorReset}")
            endif()
        endif()
    endif()

    # Set vcpkg library and include paths
    set(VCPKG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib")
    set(VCPKG_DEBUG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/debug/lib")
    set(VCPKG_INCLUDE_PATH "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include")

    # Set CMake paths to use the selected triplet
    set(CMAKE_PREFIX_PATH "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}" ${CMAKE_PREFIX_PATH})
    include_directories("${VCPKG_INCLUDE_PATH}")
    link_directories("${VCPKG_LIB_PATH}")
endif()
