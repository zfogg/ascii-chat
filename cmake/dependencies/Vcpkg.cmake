# =============================================================================
# vcpkg Configuration (Windows Only)
# =============================================================================
# Configures vcpkg package manager for Windows builds
#
# Prerequisites (must be set before including this file):
#   - WIN32: Platform detection variable
#   - CMAKE_BUILD_TYPE: Build type (Debug, Release, Dev)
#   - VCPKG_ROOT: vcpkg installation path from environment
#
# Outputs (variables set by this file):
#   - VCPKG_ROOT: vcpkg root directory path
#   - VCPKG_TRIPLET: Selected triplet (x64/arm64-windows or x64/arm64-windows-static)
#   - VCPKG_LIB_PATH: Path to vcpkg libraries (release)
#   - VCPKG_DEBUG_LIB_PATH: Path to vcpkg libraries (debug)
#   - VCPKG_INCLUDE_PATH: Path to vcpkg headers
# =============================================================================

if(WIN32)
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
        # Use triplet from environment (set by CI for ARM64 builds)
        set(_vcpkg_arch_prefix "")
        if("$ENV{VCPKG_TARGET_TRIPLET}" MATCHES "arm64")
            set(_vcpkg_arch_prefix "arm64")
        else()
            set(_vcpkg_arch_prefix "x64")
        endif()
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(_vcpkg_arch_prefix "arm64")
    else()
        set(_vcpkg_arch_prefix "x64")
    endif()

    # Determine triplet based on build type
    # Release builds use static libraries to avoid DLL dependencies
    # Debug/Dev builds use dynamic libraries for easier debugging
    if(CMAKE_BUILD_TYPE MATCHES "Release")
        set(VCPKG_TRIPLET "${_vcpkg_arch_prefix}-windows-static")
        set(VCPKG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/lib")
        set(VCPKG_DEBUG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/debug/lib")
        set(VCPKG_INCLUDE_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/include")
        message(STATUS "Using ${BoldGreen}static libraries${ColorReset} for Release build (triplet: ${BoldCyan}${VCPKG_TRIPLET}${ColorReset})")
    else()
        set(VCPKG_TRIPLET "${_vcpkg_arch_prefix}-windows")
        set(VCPKG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/lib")
        set(VCPKG_DEBUG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/debug/lib")
        set(VCPKG_INCLUDE_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/include")
        message(STATUS "Using ${BoldYellow}dynamic libraries${ColorReset} for Debug/Dev build (triplet: ${BoldCyan}${VCPKG_TRIPLET}${ColorReset})")
    endif()

    # Set CMake paths to use the selected triplet
    set(CMAKE_PREFIX_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}" ${CMAKE_PREFIX_PATH})
    include_directories("${VCPKG_INCLUDE_PATH}")
    link_directories("${VCPKG_LIB_PATH}")
endif()
