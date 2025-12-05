# =============================================================================
# Generic Dependency Finding Module
# =============================================================================
# Provides a reusable macro for finding libraries across platforms with
# consistent patterns for Windows (vcpkg), macOS (Homebrew static/pkg-config),
# and Linux (pkg-config).
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
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - VCPKG_ROOT, VCPKG_TARGET_TRIPLET, VCPKG_LIB_PATH, etc.: (Windows) vcpkg config
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

    if(WIN32)
        # =================================================================
        # Windows: Find from vcpkg
        # =================================================================
        if(_DEP_VCPKG_NAMES)
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
                set(${_DEP_NAME}_LIBRARIES
                    optimized ${${_DEP_NAME}_LIBRARY_RELEASE}
                    debug ${${_DEP_NAME}_LIBRARY_DEBUG}
                )
                set(${_DEP_NAME}_INCLUDE_DIRS ${${_DEP_NAME}_INCLUDE_DIR})
                set(${_DEP_NAME}_FOUND TRUE)
                message(STATUS "Found ${BoldGreen}${_DEP_NAME}${ColorReset}: ${${_DEP_NAME}_LIBRARY_RELEASE}")

                # Add static define if specified and using static triplet
                if(_DEP_STATIC_DEFINE AND VCPKG_TARGET_TRIPLET MATCHES "static")
                    add_compile_definitions(${_DEP_STATIC_DEFINE})
                endif()
            else()
                if(_DEP_REQUIRED)
                    message(FATAL_ERROR "Could not find ${BoldRed}${_DEP_NAME}${ColorReset} - required dependency\n"
                        "Install dependencies with: ${BoldCyan}${PROJECT_INSTALL_DEPS_SCRIPT}${ColorReset}")
                elseif(_DEP_OPTIONAL)
                    message(WARNING "Could not find ${BoldYellow}${_DEP_NAME}${ColorReset} - will continue without it")
                endif()
            endif()
        endif()

    else()
        # =================================================================
        # Unix/Linux/macOS: Use pkg-config or find static libraries
        # Skip pkg-config when using musl - dependencies built from source
        # =================================================================
        if(NOT USE_MUSL)
            # For Release builds on macOS, prefer static libraries
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
                else()
                    if(_DEP_REQUIRED)
                        message(FATAL_ERROR "Could not find static ${BoldRed}${_DEP_NAME}${ColorReset} for Release build\n"
                            "Install dependencies with: ${BoldCyan}${PROJECT_INSTALL_DEPS_SCRIPT}${ColorReset}")
                    elseif(_DEP_OPTIONAL)
                        message(WARNING "Could not find static ${BoldYellow}${_DEP_NAME}${ColorReset}")
                    endif()
                endif()

            elseif(_DEP_PKG_CONFIG)
                # Debug/Dev builds or Linux: Use pkg-config (dynamic linking is fine)
                find_package(PkgConfig QUIET REQUIRED)
                pkg_check_modules(${_DEP_NAME} QUIET IMPORTED_TARGET ${_DEP_PKG_CONFIG})

                if(${_DEP_NAME}_FOUND)
                    if(TARGET PkgConfig::${_DEP_NAME})
                        set(${_DEP_NAME}_LIBRARIES PkgConfig::${_DEP_NAME})
                    endif()
                    message(STATUS "Checking for module '${BoldBlue}${_DEP_PKG_CONFIG}${ColorReset}'")
                    message(STATUS "  Found ${BoldBlue}${_DEP_PKG_CONFIG}${ColorReset}, version ${BoldGreen}${${_DEP_NAME}_VERSION}${ColorReset}")
                else()
                    if(_DEP_REQUIRED)
                        message(FATAL_ERROR "Could not find ${BoldRed}${_DEP_PKG_CONFIG}${ColorReset} via pkg-config\n"
                            "Install dependencies with: ${BoldCyan}${PROJECT_INSTALL_DEPS_SCRIPT}${ColorReset}")
                    elseif(_DEP_OPTIONAL)
                        message(WARNING "Could not find ${BoldYellow}${_DEP_PKG_CONFIG}${ColorReset}")
                    endif()
                endif()
            endif()
        endif()
    endif()
endmacro()
