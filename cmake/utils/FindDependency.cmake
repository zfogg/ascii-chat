# =============================================================================
# Generic Dependency Finding Module
# =============================================================================
# Provides a reusable macro for finding libraries across platforms with
# consistent patterns for vcpkg, pkg-config, and Homebrew.
#
# Search Order (when USE_VCPKG=ON):
#   1. vcpkg (all platforms if USE_VCPKG=ON)
#   2. pkg-config (Linux/macOS fallback)
#   3. Homebrew static libs (macOS Release fallback)
#
# Search Order (when USE_VCPKG=OFF or not available):
#   - macOS Release: Homebrew static libs, then pkg-config
#   - macOS Debug/Dev: pkg-config
#   - Linux: pkg-config
#   - Windows: Falls back to system paths (not recommended)
#
# Usage:
#   find_dependency_library(
#       NAME <name>                    # Output variable prefix (e.g., LIBSODIUM)
#       VCPKG_NAMES <name1> [name2]... # Library names to search in vcpkg
#       HEADER <header.h>              # Header file to find
#       PKG_CONFIG <pkg-name>          # pkg-config package name
#       HOMEBREW_PKG <brew-pkg>        # Homebrew package name (for static lib path)
#       STATIC_LIB_NAME <libname.a>    # Static library filename for macOS Release
#       [REQUIRED]                     # If set, FATAL_ERROR on not found
#       [OPTIONAL]                     # If set, WARNING instead of FATAL_ERROR
#       [STATIC_DEFINE <DEFINE>]       # Define to add for static builds (e.g., SODIUM_STATIC)
#   )
#
# Prerequisites:
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_VCPKG: Whether to use vcpkg
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - VCPKG_ROOT, VCPKG_TARGET_TRIPLET, VCPKG_LIB_PATH, etc.: vcpkg config (if USE_VCPKG=ON)
#   - Colors.cmake variables for status messages
#
# Outputs:
#   - <NAME>_LIBRARIES: Libraries to link against
#   - <NAME>_INCLUDE_DIRS: Include directories
#   - <NAME>_FOUND: Whether the library was found
# =============================================================================

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_FIND_DEPENDENCY_INCLUDED)
    return()
endif()
set(_ASCIICHAT_FIND_DEPENDENCY_INCLUDED TRUE)

# =============================================================================
# Main dependency finding macro
# =============================================================================
macro(find_dependency_library)
    # Parse arguments
    set(_options REQUIRED OPTIONAL)
    set(_one_value_args NAME HEADER PKG_CONFIG HOMEBREW_PKG STATIC_LIB_NAME STATIC_DEFINE)
    set(_multi_value_args VCPKG_NAMES)
    cmake_parse_arguments(_DEP "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    # Validate required arguments
    if(NOT _DEP_NAME)
        message(FATAL_ERROR "find_dependency_library: NAME argument is required")
    endif()
    if(NOT _DEP_HEADER)
        message(FATAL_ERROR "find_dependency_library: HEADER argument is required")
    endif()

    # Initialize output variables
    set(${_DEP_NAME}_FOUND FALSE)
    set(${_DEP_NAME}_LIBRARIES "")
    set(${_DEP_NAME}_INCLUDE_DIRS "")

    # =================================================================
    # Try vcpkg first if USE_VCPKG is enabled (all platforms)
    # =================================================================
    if(USE_VCPKG AND _DEP_VCPKG_NAMES AND VCPKG_ROOT)
        find_library(${_DEP_NAME}_LIBRARY_RELEASE
            NAMES ${_DEP_VCPKG_NAMES}
            PATHS "${VCPKG_LIB_PATH}"
            NO_DEFAULT_PATH
        )
        find_library(${_DEP_NAME}_LIBRARY_DEBUG
            NAMES ${_DEP_VCPKG_NAMES}
            PATHS "${VCPKG_DEBUG_LIB_PATH}"
            NO_DEFAULT_PATH
        )
        find_path(${_DEP_NAME}_INCLUDE_DIR
            NAMES ${_DEP_HEADER}
            PATHS "${VCPKG_INCLUDE_PATH}"
            NO_DEFAULT_PATH
        )

        if(${_DEP_NAME}_LIBRARY_RELEASE OR ${_DEP_NAME}_LIBRARY_DEBUG)
            # Only add libraries that were actually found
            if(${_DEP_NAME}_LIBRARY_RELEASE AND ${_DEP_NAME}_LIBRARY_DEBUG)
                set(${_DEP_NAME}_LIBRARIES
                    optimized ${${_DEP_NAME}_LIBRARY_RELEASE}
                    debug ${${_DEP_NAME}_LIBRARY_DEBUG}
                )
            elseif(${_DEP_NAME}_LIBRARY_RELEASE)
                set(${_DEP_NAME}_LIBRARIES ${${_DEP_NAME}_LIBRARY_RELEASE})
            else()
                set(${_DEP_NAME}_LIBRARIES ${${_DEP_NAME}_LIBRARY_DEBUG})
            endif()
            set(${_DEP_NAME}_INCLUDE_DIRS ${${_DEP_NAME}_INCLUDE_DIR})
            set(${_DEP_NAME}_FOUND TRUE)
            message(STATUS "Found ${BoldGreen}${_DEP_NAME}${ColorReset} via vcpkg: ${${_DEP_NAME}_LIBRARY_RELEASE}${${_DEP_NAME}_LIBRARY_DEBUG}")

            # Add static define if specified and using static triplet
            if(_DEP_STATIC_DEFINE AND VCPKG_TARGET_TRIPLET MATCHES "static")
                add_compile_definitions(${_DEP_STATIC_DEFINE})
            endif()
        endif()
    endif()

    # =================================================================
    # Fallback: Try platform-specific methods if not found via vcpkg
    # =================================================================
    if(NOT ${_DEP_NAME}_FOUND AND NOT USE_MUSL)
        # For Release builds on macOS, prefer static libraries from Homebrew
        if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND _DEP_STATIC_LIB_NAME AND _DEP_HOMEBREW_PKG)
            # Find static library directly from Homebrew
            find_library(${_DEP_NAME}_STATIC_LIBRARY
                NAMES ${_DEP_STATIC_LIB_NAME}
                PATHS
                    /usr/local/opt/${_DEP_HOMEBREW_PKG}/lib
                    /opt/homebrew/opt/${_DEP_HOMEBREW_PKG}/lib
                NO_DEFAULT_PATH
            )
            find_path(${_DEP_NAME}_INCLUDE_DIR
                NAMES ${_DEP_HEADER}
                PATHS
                    /usr/local/opt/${_DEP_HOMEBREW_PKG}/include
                    /opt/homebrew/opt/${_DEP_HOMEBREW_PKG}/include
                NO_DEFAULT_PATH
            )

            if(${_DEP_NAME}_STATIC_LIBRARY)
                set(${_DEP_NAME}_LIBRARIES ${${_DEP_NAME}_STATIC_LIBRARY})
                set(${_DEP_NAME}_INCLUDE_DIRS ${${_DEP_NAME}_INCLUDE_DIR})
                set(${_DEP_NAME}_FOUND TRUE)
                message(STATUS "Found ${BoldBlue}${_DEP_NAME}${ColorReset} (static): ${BoldGreen}${${_DEP_NAME}_STATIC_LIBRARY}${ColorReset}")
            endif()
        endif()

        # Try pkg-config if not found yet
        if(NOT ${_DEP_NAME}_FOUND AND _DEP_PKG_CONFIG)
            # Debug/Dev builds or Linux: Use pkg-config (dynamic linking is fine)
            find_package(PkgConfig QUIET REQUIRED)
            pkg_check_modules(${_DEP_NAME} QUIET IMPORTED_TARGET ${_DEP_PKG_CONFIG})

            if(${_DEP_NAME}_FOUND)
                if(TARGET PkgConfig::${_DEP_NAME})
                    set(${_DEP_NAME}_LIBRARIES PkgConfig::${_DEP_NAME})
                endif()
                # Ensure INCLUDE_DIRS is set - pkg_check_modules may not set it if headers are in /usr/include
                if(NOT ${_DEP_NAME}_INCLUDE_DIRS)
                    # Try to get includedir from pkg-config directly
                    pkg_get_variable(_${_DEP_NAME}_INCLUDEDIR ${_DEP_PKG_CONFIG} includedir)
                    if(_${_DEP_NAME}_INCLUDEDIR)
                        set(${_DEP_NAME}_INCLUDE_DIRS ${_${_DEP_NAME}_INCLUDEDIR})
                    endif()
                endif()
                message(STATUS "Checking for module '${BoldBlue}${_DEP_PKG_CONFIG}${ColorReset}'")
                message(STATUS "  Found ${BoldBlue}${_DEP_PKG_CONFIG}${ColorReset}, version ${BoldGreen}${${_DEP_NAME}_VERSION}${ColorReset}")
            endif()
        endif()
    endif()

    # =================================================================
    # Final error handling if dependency not found
    # =================================================================
    if(NOT ${_DEP_NAME}_FOUND)
        if(_DEP_REQUIRED)
            set(_error_msg "Could not find ${BoldRed}${_DEP_NAME}${ColorReset} - required dependency\n")
            if(USE_VCPKG)
                set(_error_msg "${_error_msg}Tried vcpkg, pkg-config, and Homebrew\n")
            endif()
            set(_error_msg "${_error_msg}Install dependencies with: ${BoldCyan}${PROJECT_INSTALL_DEPS_SCRIPT}${ColorReset}")
            message(FATAL_ERROR ${_error_msg})
        elseif(_DEP_OPTIONAL)
            message(WARNING "Could not find ${BoldYellow}${_DEP_NAME}${ColorReset} - will continue without it")
        endif()
    endif()
endmacro()
