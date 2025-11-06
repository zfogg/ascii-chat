# =============================================================================
# zstd Compression Library Configuration
# =============================================================================
# Finds and configures zstd compression library
#
# Platform-specific dependency management:
#   - Windows: Uses vcpkg
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Linux (musl): Dependencies built from source (see MuslDependencies.cmake)
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - VCPKG_ROOT, VCPKG_TRIPLET: (Windows only) vcpkg configuration
#
# Outputs (variables set by this file):
#   - ZSTD_LIBRARIES: Libraries to link against
#   - ZSTD_INCLUDE_DIRS: Include directories
#   - ZSTD_FOUND: Whether zstd was found
# =============================================================================

if(WIN32)
    # Windows: Find zstd from vcpkg
    find_library(ZSTD_LIBRARY_RELEASE NAMES zstd zstd_static PATHS "${VCPKG_LIB_PATH}" NO_DEFAULT_PATH)
    find_library(ZSTD_LIBRARY_DEBUG NAMES zstdd zstd zstd_static PATHS "${VCPKG_DEBUG_LIB_PATH}" NO_DEFAULT_PATH)
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h PATHS "${VCPKG_INCLUDE_PATH}" NO_DEFAULT_PATH)

    if(ZSTD_LIBRARY_RELEASE OR ZSTD_LIBRARY_DEBUG)
        set(ZSTD_LIBRARIES optimized ${ZSTD_LIBRARY_RELEASE} debug ${ZSTD_LIBRARY_DEBUG})
        set(ZSTD_INCLUDE_DIRS ${ZSTD_INCLUDE_DIR})
        set(ZSTD_FOUND TRUE)
        message(STATUS "Found ${BoldGreen}zstd${ColorReset}: ${ZSTD_LIBRARY_RELEASE}")

        # Define ZSTD_STATIC for static builds to prevent dllimport
        if(CMAKE_BUILD_TYPE MATCHES "Release")
            add_compile_definitions(ZSTD_STATIC)
        endif()
    else()
        message(FATAL_ERROR "Could not find ${BoldRed}zstd${ColorReset} - required dependency")
    endif()
else()
    # Unix/Linux/macOS: Use pkg-config or find static libraries
    # Skip pkg-config when using musl - dependencies are built from source
    if(NOT USE_MUSL)
        # For Release builds on macOS, prefer static libraries
        if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release")
            # Find static library directly from Homebrew
            find_library(ZSTD_STATIC_LIBRARY NAMES libzstd.a
                PATHS /usr/local/opt/zstd/lib /opt/homebrew/opt/zstd/lib
                NO_DEFAULT_PATH
            )
            find_path(ZSTD_INCLUDE_DIR NAMES zstd.h
                PATHS /usr/local/opt/zstd/include /opt/homebrew/opt/zstd/include
                NO_DEFAULT_PATH
            )

            if(ZSTD_STATIC_LIBRARY)
                set(ZSTD_LIBRARIES ${ZSTD_STATIC_LIBRARY})
                set(ZSTD_INCLUDE_DIRS ${ZSTD_INCLUDE_DIR})
                set(ZSTD_FOUND TRUE)
                message(STATUS "Found ${BoldBlue}libzstd${ColorReset} (static): ${BoldGreen}${ZSTD_STATIC_LIBRARY}${ColorReset}")
            else()
                message(FATAL_ERROR "Could not find static ${BoldRed}libzstd${ColorReset} for Release build")
            endif()
        else()
            # Debug/Dev builds: Use pkg-config (dynamic linking is fine)
            # On macOS, prefer Homebrew zstd over system zstd for consistency
            if(APPLE)
                # Check for Homebrew zstd first
                if(EXISTS "/usr/local/opt/zstd/lib/pkgconfig/libzstd.pc")
                    set(ENV{PKG_CONFIG_PATH} "/usr/local/opt/zstd/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
                    message(STATUS "Using ${BoldBlue}Homebrew${ColorReset} ${BoldBlue}zstd${ColorReset} from /usr/local/opt/zstd")
                elseif(EXISTS "/opt/homebrew/opt/zstd/lib/pkgconfig/libzstd.pc")
                    set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/zstd/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
                    message(STATUS "Using ${BoldBlue}Homebrew${ColorReset} ${BoldBlue}zstd${ColorReset} from /opt/homebrew/opt/zstd")
                endif()
            endif()

            find_package(PkgConfig QUIET REQUIRED)
            pkg_check_modules(ZSTD REQUIRED QUIET libzstd)
            if(ZSTD_FOUND)
                message(STATUS "Checking for module '${BoldBlue}libzstd${ColorReset}'")
                message(STATUS "  Found ${BoldBlue}libzstd${ColorReset}, version ${BoldGreen}${ZSTD_VERSION}${ColorReset}")
            endif()
        endif()
    endif()
endif()
