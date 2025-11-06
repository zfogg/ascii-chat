# =============================================================================
# Initialization Module
# =============================================================================
# This module handles early initialization that must happen before project():
# - Policy settings
# - Dependency cache configuration
# - Build system generator setup
# - Compiler testing configuration
#
# Prerequisites: None (runs before project())
# Note: cmake_minimum_required must be in CMakeLists.txt before this include
# =============================================================================

# =============================================================================
# Compiler Detection (before project())
# =============================================================================
# Force Clang compiler - MSVC and GCC are not supported
# Supports Windows (scoop/official LLVM), macOS (Homebrew), and Linux (system)
if(NOT CMAKE_C_COMPILER)
    if(WIN32)
        # Windows: Try to find Clang from common installation locations
        find_program(CLANG_EXECUTABLE
            NAMES clang clang.exe
            PATHS
                "$ENV{PROGRAMFILES}/LLVM/bin"
                "$ENV{LOCALAPPDATA}/Programs/LLVM/bin"
                "C:/Program Files/LLVM/bin"
                "$ENV{USERPROFILE}/scoop/apps/llvm/current/bin"
                "$ENV{USERPROFILE}/scoop/shims"
        )

        if(NOT CLANG_EXECUTABLE)
            message(FATAL_ERROR "Clang not found. Install Clang:\n"
                               "  Windows: scoop install llvm\n"
                               "  Or download from: https://llvm.org/builds/")
        endif()
    elseif(APPLE)
        # macOS: Prefer Homebrew Clang over Apple's Clang
        find_program(CLANG_EXECUTABLE
            NAMES clang
            PATHS
                /opt/homebrew/opt/llvm/bin      # Apple Silicon Homebrew
                /usr/local/opt/llvm/bin         # Intel Homebrew
                /usr/bin                        # System Clang (fallback)
            NO_DEFAULT_PATH
        )

        if(NOT CLANG_EXECUTABLE)
            # Fallback to system clang if Homebrew not found
            find_program(CLANG_EXECUTABLE NAMES clang)
        endif()

        if(NOT CLANG_EXECUTABLE)
            message(FATAL_ERROR "Clang not found. Install Clang:\n"
                               "  macOS: brew install llvm\n"
                               "  Or use system clang from Xcode Command Line Tools")
        endif()
    else()
        # Linux/Unix: Use system Clang (support versions 21 down to 15)
        find_program(CLANG_EXECUTABLE
            NAMES clang clang-21 clang-20 clang-19 clang-18 clang-17 clang-16 clang-15
        )

        if(NOT CLANG_EXECUTABLE)
            message(FATAL_ERROR "Clang not found. Install Clang:\n"
                               "  Debian/Ubuntu: sudo apt install clang\n"
                               "  Fedora/RHEL: sudo dnf install clang\n"
                               "  Arch: sudo pacman -S clang")
        endif()
    endif()

    # Set the compiler (all platforms)
    set(CMAKE_C_COMPILER "${CLANG_EXECUTABLE}" CACHE FILEPATH "C compiler" FORCE)

    # Derive CXX compiler path from C compiler
    if(WIN32)
        # Windows: Replace clang.exe with clang++.exe
        string(REPLACE "clang.exe" "clang++.exe" CLANGXX_EXECUTABLE "${CLANG_EXECUTABLE}")
    else()
        # Unix/macOS: Append ++ to clang
        set(CLANGXX_EXECUTABLE "${CLANG_EXECUTABLE}++")
    endif()

    set(CMAKE_CXX_COMPILER "${CLANGXX_EXECUTABLE}" CACHE FILEPATH "CXX compiler" FORCE)
    message(STATUS "Set default C compiler to ${BoldCyan}Clang${ColorReset}: ${CLANG_EXECUTABLE}")
    message(STATUS "Set default C++ compiler to ${BoldCyan}Clang++${ColorReset}: ${CLANGXX_EXECUTABLE}")
endif()

# =============================================================================
# CMake Policy Configuration
# =============================================================================
# Set modern CMake policies for future-proofing and best practices
# These policies enable newer CMake features and behaviors

# Define colors for terminal output
# Import color definitions from Colors.cmake
include(${CMAKE_SOURCE_DIR}/cmake/utils/Colors.cmake)

# Detect CPU cores for parallel builds
# Note: Use CMAKE_HOST_SYSTEM_NAME because CMAKE_SYSTEM_NAME is not set until after project()
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    execute_process(COMMAND sysctl -n hw.logicalcpu OUTPUT_VARIABLE CPU_CORES OUTPUT_STRIP_TRAILING_WHITESPACE)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    execute_process(COMMAND nproc OUTPUT_VARIABLE CPU_CORES OUTPUT_STRIP_TRAILING_WHITESPACE)
elseif(WIN32)
    # Windows: Use environment variable or wmic
    if(DEFINED ENV{NUMBER_OF_PROCESSORS})
        set(CPU_CORES $ENV{NUMBER_OF_PROCESSORS})
    else()
        execute_process(COMMAND wmic cpu get NumberOfLogicalProcessors /value
                       OUTPUT_VARIABLE CPU_INFO OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "NumberOfLogicalProcessors=([0-9]+)" _ ${CPU_INFO})
        if(CMAKE_MATCH_1)
            set(CPU_CORES ${CMAKE_MATCH_1})
        else()
            set(CPU_CORES 4)
        endif()
    endif()
else()
    set(CPU_CORES 4)
endif()

# Set parallel build level automatically if not already set
if(NOT DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} ${CPU_CORES})
endif()
message(STATUS "Parallel build jobs: ${BoldCyan}${CPU_CORES}${ColorReset}")

# CMP0135: ExternalProject step targets fully imported
# Enables proper dependency tracking for ExternalProject steps
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# CMP0144: FindPackage uses upper-case <PackageName>_ROOT
# Modern way to specify package root directories
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

# CMP0146: ExternalProject step targets fully imported
# Better integration with ExternalProject and FetchContent
if(POLICY CMP0146)
    cmake_policy(SET CMP0146 NEW)
endif()

# CMP0169: Ninja generator uses link dependencies
# Improves build dependency tracking with Ninja
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 NEW)
endif()

# CMP0251: CMAKE_SYSROOT is read-only
# Prevents accidental modification of sysroot
if(POLICY CMP0251)
    cmake_policy(SET CMP0251 NEW)
endif()

# =============================================================================
# Dependency Cache Configuration (persistent across build/ deletions)
# =============================================================================
# Cache FetchContent dependencies outside build/ directory to avoid recompiling
# them every time build/ is deleted. Dependencies are compiled once and reused.
#
# Separate cache directories for different build configurations:
# - .deps-cache/<BuildType>/           : Native builds (normal glibc/system libc)
# - .deps-cache/musl/                  : Native musl builds
# - .deps-cache-docker/<BuildType>/    : Docker builds
# - .deps-cache-docker/musl/           : Docker musl builds
#
# Build types need separate caches because dependencies like mimalloc have different
# configurations (Debug: MI_DEBUG_FULL=ON, Release: MI_DEBUG_FULL=OFF)
#
# To force rebuild dependencies: rm -rf .deps-cache* (or just .deps-cache-docker for Docker)

# Use centralized dependency cache variables from CMakeLists.txt
# DEPS_CACHE_ROOT, DEPS_CACHE_DIR, and DEPS_CACHE_MUSL are set in CMakeLists.txt

# FetchContent deps (mimalloc, bearssl, etc.) use the build-type-specific cache
set(FETCHCONTENT_BASE_DIR "${DEPS_CACHE_DIR}" CACHE PATH "FetchContent cache directory")

# Note: MUSL_DEPS_DIR_STATIC is set in Musl.cmake's configure_musl_post_project()
# after USE_MUSL is defined (can't be set here since this runs before project())

# =============================================================================
# Build System Generator Configuration (MUST be before vcpkg toolchain)
# =============================================================================

# Use Ninja generator by default on all platforms for faster builds
# Only set Ninja if no generator was explicitly specified via -G flag
# This MUST be set before vcpkg toolchain to prevent vcpkg from forcing Visual Studio generator
if(NOT CMAKE_GENERATOR AND NOT DEFINED CMAKE_GENERATOR_INTERNAL)
    find_program(NINJA_EXECUTABLE ninja)
    if(NINJA_EXECUTABLE)
        set(CMAKE_GENERATOR "Ninja" CACHE STRING "Build system generator" FORCE)
        message(STATUS "Using ${BoldGreen}Ninja${ColorReset} generator for faster builds")
    endif()
endif()

# =============================================================================
# vcpkg Toolchain Setup
# =============================================================================
# Set up vcpkg toolchain if available (must be before project())
if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        message(STATUS "Using vcpkg toolchain from environment: $ENV{VCPKG_ROOT}")
    endif()
endif()

# On macOS, prefer gmake over make when using Unix Makefiles generator
if(APPLE AND CMAKE_GENERATOR MATCHES "Unix Makefiles")
    find_program(GMAKE_EXECUTABLE gmake)
    if(GMAKE_EXECUTABLE)
        set(CMAKE_MAKE_PROGRAM "${GMAKE_EXECUTABLE}" CACHE FILEPATH "Make program" FORCE)
        message(STATUS "Using gmake: ${GMAKE_EXECUTABLE}")
    endif()
endif()

# =============================================================================
# Compiler Testing Configuration
# =============================================================================
# Speed up CMake's compiler tests by avoiding linking issues
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

