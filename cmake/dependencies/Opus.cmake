# =============================================================================
# Opus Library Configuration
# =============================================================================
# Finds and configures Opus audio codec library
#
# Opus is used for:
#   - Real-time audio compression for network transmission
#   - Low-latency voice and music encoding/decoding
#   - Efficient bandwidth usage in multi-client audio chat
#
# Build strategy:
#   - For musl: Built from source in MuslDependencies.cmake
#   - For glibc: Uses system-installed Opus via pkg-config
#
# Prerequisites (must be set before including this file):
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#
# Outputs (variables set by this file):
#   - OPUS_FOUND: Whether Opus was found
#   - OPUS_LIBRARIES: Libraries to link against
#   - OPUS_INCLUDE_DIRS: Include directories
# =============================================================================

# Skip for musl builds - Opus is already configured in MuslDependencies.cmake
if(USE_MUSL)
    return()
endif()

# For Windows: Try to find Opus via Vcpkg or pkg-config
if(WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPUS opus IMPORTED_TARGET)
        if(OPUS_FOUND)
            message(STATUS "${BoldGreen}Opus${ColorReset} found via pkg-config (Windows): ${OPUS_LIBRARIES}")
            return()
        endif()
    endif()

    # Try to find Opus from Vcpkg
    find_library(OPUS_LIB NAMES opus libopus
                 PATHS "${_VCPKG_INSTALLED_DIR}" NO_DEFAULT_PATH)
    find_path(OPUS_INC NAMES opus/opus.h
              PATHS "${_VCPKG_INSTALLED_DIR}" NO_DEFAULT_PATH)

    if(OPUS_LIB AND OPUS_INC)
        set(OPUS_FOUND TRUE)
        set(OPUS_LIBRARIES "${OPUS_LIB}")
        set(OPUS_INCLUDE_DIRS "${OPUS_INC}")
        message(STATUS "${BoldGreen}Opus${ColorReset} found (Windows Vcpkg): ${OPUS_LIB}")
        return()
    endif()

    message(FATAL_ERROR "${BoldRed}Opus not found on Windows${ColorReset}. Install with:\n"
        "  vcpkg: vcpkg install opus:x64-windows\n"
        "  Chocolatey: choco install opus\n"
        "  Or via pkg-config with MSYS2/MinGW")
endif()

# Unix/Linux/macOS: Use pkg-config
find_package(PkgConfig QUIET)
if(NOT PkgConfig_FOUND)
    message(FATAL_ERROR "pkg-config not found. Required to find system Opus library.")
endif()

pkg_check_modules(OPUS REQUIRED opus IMPORTED_TARGET)

if(OPUS_FOUND)
    message(STATUS "${BoldGreen}Opus${ColorReset} found via pkg-config: ${OPUS_LIBRARIES}")
else()
    message(FATAL_ERROR "${BoldRed}Opus not found${ColorReset}. Install with:\n"
        "  Ubuntu/Debian: sudo apt install libopus-dev\n"
        "  Fedora: sudo dnf install opus-devel\n"
        "  Arch: sudo pacman -S opus\n"
        "  macOS: brew install opus")
endif()
