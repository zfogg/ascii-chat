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
# Output:
#   - asciichat-${NAME}: canonical INTERFACE target for consumers
# =============================================================================

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_FIND_DEPENDENCY_INCLUDED)
    return()
endif()
set(_ASCIICHAT_FIND_DEPENDENCY_INCLUDED TRUE)

# =============================================================================
# Main dependency finding function
# =============================================================================
function(find_dependency_library)
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

    # Keep discovery state private to this function.  The public contract is
    # the canonical asciichat-* target created below; leaking a package's
    # historical *_FOUND/*_LIBRARIES/*_INCLUDE_DIRS variables makes unrelated
    # dependency modules order-dependent.
    set(_dep_found FALSE)
    set(_dep_libraries "")
    set(_dep_include_dirs "")

    # =================================================================
    # Try vcpkg first if USE_VCPKG is enabled (all platforms)
    # =================================================================
    if(USE_VCPKG AND _DEP_VCPKG_NAMES AND VCPKG_ROOT)
        find_library(_dep_library_release
            NAMES ${_DEP_VCPKG_NAMES}
            PATHS "${VCPKG_LIB_PATH}"
            NO_DEFAULT_PATH
        )
        find_library(_dep_library_debug
            NAMES ${_DEP_VCPKG_NAMES}
            PATHS "${VCPKG_DEBUG_LIB_PATH}"
            NO_DEFAULT_PATH
        )
        find_path(_dep_include_dir
            NAMES ${_DEP_HEADER}
            PATHS "${VCPKG_INCLUDE_PATH}"
            NO_DEFAULT_PATH
        )

        if(_dep_library_release OR _dep_library_debug)
            include(${CMAKE_SOURCE_DIR}/cmake/utils/SelectLibraryConfig.cmake)
            asciichat_select_library_config(
                RELEASE_LIB ${_dep_library_release}
                DEBUG_LIB   ${_dep_library_debug}
                OUTPUT      _dep_libraries
            )
            set(_dep_include_dirs ${_dep_include_dir})
            set(_dep_found TRUE)
            message(STATUS "Found ${BoldGreen}${_DEP_NAME}${ColorReset} via vcpkg: ${_dep_library_release}${_dep_library_debug}")

            # Add static define if specified and using static triplet
            if(_DEP_STATIC_DEFINE AND VCPKG_TARGET_TRIPLET MATCHES "static")
                add_compile_definitions(${_DEP_STATIC_DEFINE})
            endif()
        endif()
    endif()

    # =================================================================
    # Fallback: Try platform-specific methods if not found via vcpkg
    # =================================================================
    if(NOT _dep_found AND NOT USE_MUSL)
        # For Release builds on macOS, prefer static libraries from Homebrew
        # Skip if shared libraries are disabled (e.g., static package builds).
        if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND _DEP_STATIC_LIB_NAME AND _DEP_HOMEBREW_PKG AND NOT BUILD_SHARED_LIBS)
            # Find static library directly from Homebrew
            find_library(_dep_static_library
                NAMES ${_DEP_STATIC_LIB_NAME}
                PATHS
                    ${HOMEBREW_PREFIX}/opt/${_DEP_HOMEBREW_PKG}/lib
                NO_DEFAULT_PATH
            )
            find_path(_dep_include_dir
                NAMES ${_DEP_HEADER}
                PATHS
                    ${HOMEBREW_PREFIX}/opt/${_DEP_HOMEBREW_PKG}/include
                NO_DEFAULT_PATH
            )

            if(_dep_static_library)
                set(_dep_libraries ${_dep_static_library})
                set(_dep_include_dirs ${_dep_include_dir})
                # Extract library directory from full library path for target_link_directories()
                get_filename_component(_dep_library_dirs ${_dep_static_library} DIRECTORY)
                set(_dep_found TRUE)
                message(STATUS "Found ${BoldBlue}${_DEP_NAME}${ColorReset} (static): ${BoldGreen}${_dep_static_library}${ColorReset}")
            endif()
        endif()

        # Try pkg-config if not found yet
        if(NOT _dep_found AND _DEP_PKG_CONFIG)
            # Use pkg-config if available (not required - vcpkg handles deps on Windows)
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(_asciichat_dep_pkg QUIET IMPORTED_TARGET GLOBAL
                    ${_DEP_PKG_CONFIG})
            endif()

            if(_asciichat_dep_pkg_FOUND)
                if(TARGET PkgConfig::_asciichat_dep_pkg)
                    set(_dep_libraries PkgConfig::_asciichat_dep_pkg)
                elseif(_asciichat_dep_pkg_LINK_LIBRARIES)
                    set(_dep_libraries ${_asciichat_dep_pkg_LINK_LIBRARIES})
                endif()
                set(_dep_include_dirs ${_asciichat_dep_pkg_INCLUDE_DIRS})
                # Ensure INCLUDE_DIRS is set - pkg_check_modules may not set it if headers are in /usr/include
                if(NOT _dep_include_dirs)
                    # Try to get includedir from pkg-config directly
                    pkg_get_variable(_dep_pkg_includedir ${_DEP_PKG_CONFIG} includedir)
                    if(_dep_pkg_includedir)
                        set(_dep_include_dirs ${_dep_pkg_includedir})
                    endif()
                endif()
                set(_dep_found TRUE)
                message(STATUS "Checking for module '${BoldBlue}${_DEP_PKG_CONFIG}${ColorReset}'")
                message(STATUS "  Found ${BoldBlue}${_DEP_PKG_CONFIG}${ColorReset}, version ${BoldGreen}${_asciichat_dep_pkg_VERSION}${ColorReset}")
            endif()
        endif()
    endif()

    # =================================================================
    # Final error handling if dependency not found
    # =================================================================
    if(NOT _dep_found)
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

    # Every dependency discovered through this helper exposes one stable,
    # target-only interface.  Consumers must not need to know whether the
    # provider came from vcpkg, pkg-config, Homebrew, or an imported archive.
    if(_dep_found)
        string(TOLOWER "${_DEP_NAME}" _dep_target_name)
        set(_dep_target "asciichat-${_dep_target_name}")
        if(NOT TARGET "${_dep_target}")
            add_library("${_dep_target}" INTERFACE)
            if(_dep_libraries)
                target_link_libraries("${_dep_target}" INTERFACE ${_dep_libraries})
            endif()
            if(_dep_include_dirs)
                target_include_directories("${_dep_target}" INTERFACE ${_dep_include_dirs})
            endif()
        endif()
    endif()
endfunction()

