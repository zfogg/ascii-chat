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
# CMake Policy Configuration
# =============================================================================
# Set modern CMake policies for future-proofing and best practices
# These policies enable newer CMake features and behaviors

# Define colors for terminal output
# Import color definitions from Colors.cmake
include(${CMAKE_SOURCE_DIR}/cmake/utils/Colors.cmake)

# Centralized program discovery - find all external programs once
# Other modules should use the ASCIICHAT_* cached variables instead of find_program
include(${CMAKE_SOURCE_DIR}/cmake/init/FindPrograms.cmake)

# Detect CPU cores for parallel builds
# Note: Use CMAKE_HOST_SYSTEM_NAME because CMAKE_SYSTEM_NAME is not set until after project()
set(_ASCII_CPU_FALLBACK 4)
set(CPU_CORES "")

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    execute_process(
        COMMAND sysctl -n hw.logicalcpu
        OUTPUT_VARIABLE CPU_CORES
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _cpu_cmd_status
    )
    if(NOT _cpu_cmd_status EQUAL 0)
        message(WARNING "Failed to query CPU core count via sysctl; falling back to ${_ASCII_CPU_FALLBACK}")
        set(CPU_CORES "")
    endif()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    execute_process(
        COMMAND nproc
        OUTPUT_VARIABLE CPU_CORES
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _cpu_cmd_status
    )
    if(NOT _cpu_cmd_status EQUAL 0)
        message(WARNING "Failed to query CPU core count via nproc; falling back to ${_ASCII_CPU_FALLBACK}")
        set(CPU_CORES "")
    endif()
elseif(WIN32)
    # Windows: Use environment variable or wmic
    if(DEFINED ENV{NUMBER_OF_PROCESSORS} AND NOT "$ENV{NUMBER_OF_PROCESSORS}" STREQUAL "")
        set(CPU_CORES $ENV{NUMBER_OF_PROCESSORS})
    else()
        execute_process(
            COMMAND wmic cpu get NumberOfLogicalProcessors /value
            OUTPUT_VARIABLE CPU_INFO
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _cpu_cmd_status
        )
        if(_cpu_cmd_status EQUAL 0)
            string(REGEX MATCH "NumberOfLogicalProcessors=([0-9]+)" _ ${CPU_INFO})
            if(CMAKE_MATCH_1)
                set(CPU_CORES ${CMAKE_MATCH_1})
            endif()
        else()
            message(WARNING "Failed to query CPU core count via wmic; falling back to ${_ASCII_CPU_FALLBACK}")
        endif()
    endif()
endif()

if(CPU_CORES STREQUAL "" OR NOT CPU_CORES MATCHES "^[0-9]+$")
    if(NOT CPU_CORES STREQUAL "")
        message(WARNING "CPU core detection returned unexpected value '${CPU_CORES}'; falling back to ${_ASCII_CPU_FALLBACK}")
    endif()
    set(CPU_CORES ${_ASCII_CPU_FALLBACK})
elseif(CPU_CORES LESS 1)
    message(WARNING "CPU core detection returned non-positive value '${CPU_CORES}'; falling back to ${_ASCII_CPU_FALLBACK}")
    set(CPU_CORES ${_ASCII_CPU_FALLBACK})
endif()

# Set parallel build level automatically if not already set
if(NOT DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} ${CPU_CORES})
endif()
if(CPU_CORES GREATER 0)
    message(STATUS "Parallel build jobs: ${BoldCyan}${CPU_CORES}${ColorReset}")
else()
    message(FATAL_ERROR "Failed to detect CPU core count; please set CPU_CORES to the number of cores for parallel builds")
endif()

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
# - .deps-cache/docker/<BuildType>/    : Docker builds
# - .deps-cache/docker/musl/           : Docker musl builds
#
# Build types need separate caches because dependencies like mimalloc have different
# configurations (Debug: MI_DEBUG_FULL=ON, Release: MI_DEBUG_FULL=OFF)
#
# To force rebuild dependencies: rm -rf .deps-cache/ (or just .deps-cache/docker/ for Docker)

# Use centralized dependency cache variables from CMakeLists.txt
# ASCIICHAT_DEPS_CACHE_ROOT, ASCIICHAT_DEPS_CACHE_DIR, and ASCIICHAT_DEPS_CACHE_MUSL are set in CMakeLists.txt

# FetchContent deps (mimalloc, bearssl, etc.) use the build-type-specific cache
# Note: Use FORCE since this is computed from ASCIICHAT_DEPS_CACHE_DIR which may change
set(FETCHCONTENT_BASE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}" CACHE PATH "FetchContent cache directory" FORCE)

# Note: MUSL_DEPS_DIR_STATIC is set in Musl.cmake's configure_musl_post_project()
# after USE_MUSL is defined (can't be set here since this runs before project())

# =============================================================================
# Build System Generator Configuration (MUST be before vcpkg toolchain)
# =============================================================================

# Use Ninja generator by default on all platforms for faster builds
# Only set Ninja if no generator was explicitly specified via -G flag
# This MUST be set before vcpkg toolchain to prevent vcpkg from forcing Visual Studio generator
if(NOT CMAKE_GENERATOR AND NOT DEFINED CMAKE_GENERATOR_INTERNAL)
    # Use centralized ASCIICHAT_NINJA_EXECUTABLE from FindPrograms.cmake
    if(ASCIICHAT_NINJA_EXECUTABLE)
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
        # Disable vcpkg's applocal.ps1 DLL copying BEFORE loading toolchain
        # (slow PowerShell startup adds ~1-2s per link)
        # We handle DLL copying ourselves using cmd /c in PostBuild.cmake
        set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Disable vcpkg automatic DLL copying" FORCE)

        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        message(STATUS "Using ${BoldGreen}vcpkg${ColorReset} toolchain from environment: ${BoldBlue}$ENV{VCPKG_ROOT}${ColorReset}")
        message(STATUS "Disabled vcpkg applocal.ps1 - using ${BoldCyan}cmd /c xcopy${ColorReset} for DLL copying")
    endif()
endif()

# On macOS, prefer gmake over make when using Unix Makefiles generator
if(APPLE AND CMAKE_GENERATOR MATCHES "Unix Makefiles")
    # Use centralized ASCIICHAT_GMAKE_EXECUTABLE from FindPrograms.cmake
    if(ASCIICHAT_GMAKE_EXECUTABLE)
        set(CMAKE_MAKE_PROGRAM "${ASCIICHAT_GMAKE_EXECUTABLE}" CACHE FILEPATH "Make program" FORCE)
        message(STATUS "Using gmake: ${ASCIICHAT_GMAKE_EXECUTABLE}")
    endif()
endif()

# =============================================================================
# Compiler Testing Configuration
# =============================================================================
# Speed up CMake's compiler tests by avoiding linking issues
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)


