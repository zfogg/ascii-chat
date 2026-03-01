# =============================================================================
# MuslDep.cmake - Common setup and utilities for musl dependency builds
# =============================================================================
# This file provides shared setup for musl builds. Individual dependency files
# (Zstd.cmake, Yyjson.cmake, etc.) handle their own musl-specific build logic
# using the variables and functions defined here.
#
# When USE_MUSL=ON, dependencies are built from source instead of using system
# packages. This file validates required tools and sets up common variables.
# =============================================================================

if(NOT USE_MUSL)
    return()
endif()

# =============================================================================
# Validate required programs for musl builds
# =============================================================================
if(NOT ASCIICHAT_MUSL_GCC_EXECUTABLE)
    message(FATAL_ERROR "musl-gcc not found. Required for Linux Release builds with USE_MUSL=ON.\n"
        "Install musl development tools:\n"
        "  Arch Linux: sudo pacman -S musl\n"
        "  Ubuntu/Debian: sudo apt install musl-tools\n"
        "  Fedora: sudo dnf install musl-gcc")
endif()

if(NOT ASCIICHAT_GCC_EXECUTABLE)
    message(FATAL_ERROR "gcc not found. Required by musl-gcc (via REALGCC).\n"
        "Install GCC:\n"
        "  Arch Linux: sudo pacman -S gcc\n"
        "  Ubuntu/Debian: sudo apt install gcc\n"
        "  Fedora: sudo dnf install gcc")
endif()

# Set variables for use in ExternalProject commands
set(MUSL_GCC "${ASCIICHAT_MUSL_GCC_EXECUTABLE}")
set(REAL_GCC "${ASCIICHAT_GCC_EXECUTABLE}")

message(STATUS "Building dependencies from source for musl libc...")
message(STATUS "  musl-gcc: ${MUSL_GCC}")
message(STATUS "  gcc (REALGCC): ${REAL_GCC}")

include(FetchContent)

# Validate that MUSL_DEPS_DIR_STATIC is properly set
if(NOT MUSL_DEPS_DIR_STATIC OR MUSL_DEPS_DIR_STATIC STREQUAL "" OR MUSL_DEPS_DIR_STATIC STREQUAL "/")
    message(FATAL_ERROR "MUSL_DEPS_DIR_STATIC is not properly set (value: '${MUSL_DEPS_DIR_STATIC}'). "
                        "This usually means CMAKE_BUILD_TYPE was empty when ASCIICHAT_DEPS_CACHE_* was configured. "
                        "Please specify -DCMAKE_BUILD_TYPE=Release (or Debug) on the command line.")
endif()

# =============================================================================
# Kernel headers for musl (needed for ALSA, V4L2, etc.)
# =============================================================================
set(KERNEL_HEADERS_DIR "${MUSL_DEPS_DIR_STATIC}/kernel-headers")

# Find and copy kernel headers if needed
set(KERNEL_HEADER_FOUND FALSE)
set(KERNEL_HEADER_SEARCH_PATHS
    "/usr/include/linux"
    "/usr/include/x86_64-linux-gnu/asm"
    "/usr/include/asm"
    "/usr/include/asm-generic"
)

foreach(HEADER_PATH ${KERNEL_HEADER_SEARCH_PATHS})
    if(EXISTS "${HEADER_PATH}")
        set(KERNEL_HEADER_FOUND TRUE)
        break()
    endif()
endforeach()

if(KERNEL_HEADER_FOUND)
    if(NOT EXISTS "${KERNEL_HEADERS_DIR}/linux")
        message(STATUS "Copying kernel headers to ${KERNEL_HEADERS_DIR}...")
        file(MAKE_DIRECTORY "${KERNEL_HEADERS_DIR}")

        if(EXISTS "/usr/include/linux")
            file(COPY "/usr/include/linux" DESTINATION "${KERNEL_HEADERS_DIR}")
        endif()

        # Detect architecture-specific header path
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
            set(_ASM_ARCH_PATH "/usr/include/aarch64-linux-gnu/asm")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set(_ASM_ARCH_PATH "/usr/include/x86_64-linux-gnu/asm")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i686|i386")
            set(_ASM_ARCH_PATH "/usr/include/i386-linux-gnu/asm")
        else()
            set(_ASM_ARCH_PATH "")
        endif()

        if(_ASM_ARCH_PATH AND EXISTS "${_ASM_ARCH_PATH}")
            file(COPY "${_ASM_ARCH_PATH}" DESTINATION "${KERNEL_HEADERS_DIR}")
        elseif(EXISTS "/usr/include/asm")
            file(COPY "/usr/include/asm" DESTINATION "${KERNEL_HEADERS_DIR}")
        else()
            message(STATUS "No arch-specific asm headers found, creating symlink to asm-generic")
            file(CREATE_LINK "${KERNEL_HEADERS_DIR}/asm-generic" "${KERNEL_HEADERS_DIR}/asm" SYMBOLIC)
        endif()

        if(EXISTS "/usr/include/asm-generic")
            file(COPY "/usr/include/asm-generic" DESTINATION "${KERNEL_HEADERS_DIR}")
        endif()

        message(STATUS "Kernel headers copied successfully")
    else()
        message(STATUS "Using cached ${BoldBlue}kernel headers${ColorReset} from ${BoldMagenta}${KERNEL_HEADERS_DIR}${ColorReset}")
    endif()

    set(MUSL_KERNEL_CFLAGS "-fPIC -I${KERNEL_HEADERS_DIR}")
else()
    message(WARNING "Kernel headers not found in common locations. Install linux-libc-dev or kernel-headers package.")
endif()

# Save current output directories (restored by individual dependency files)
set(_SAVED_ARCHIVE_OUTPUT_DIR ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
set(_SAVED_LIBRARY_OUTPUT_DIR ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
