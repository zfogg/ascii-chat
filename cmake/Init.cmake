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
# Modern Windows terminals (Windows Terminal, PowerShell 7+, ConEmu) support ANSI colors
# Windows 10+ natively supports ANSI escape codes, so enable colors by default
if(WIN32)
    # Enable colors on Windows - modern terminals (Windows Terminal, PowerShell 7+, ConEmu)
    # and Windows 10+ native console all support ANSI escape codes
    string(ASCII 27 Esc)
    set(ColorReset "${Esc}[m")
    set(ColorBold "${Esc}[1m")
    set(Red "${Esc}[31m")
    set(Green "${Esc}[32m")
    set(Yellow "${Esc}[33m")
    set(Blue "${Esc}[34m")
    set(Magenta "${Esc}[35m")
    set(Cyan "${Esc}[36m")
    set(White "${Esc}[37m")
    set(BoldRed "${Esc}[1;31m")
    set(BoldGreen "${Esc}[1;32m")
    set(BoldYellow "${Esc}[1;33m")
    set(BoldBlue "${Esc}[1;34m")
    set(BoldMagenta "${Esc}[1;35m")
    set(BoldCyan "${Esc}[1;36m")
    set(BoldWhite "${Esc}[1;37m")
else()
    # Non-Windows: Always enable colors (standard ANSI support)
    string(ASCII 27 Esc)
    set(ColorReset "${Esc}[m")
    set(ColorBold "${Esc}[1m")
    set(Red "${Esc}[31m")
    set(Green "${Esc}[32m")
    set(Yellow "${Esc}[33m")
    set(Blue "${Esc}[34m")
    set(Magenta "${Esc}[35m")
    set(Cyan "${Esc}[36m")
    set(White "${Esc}[37m")
    set(BoldRed "${Esc}[1;31m")
    set(BoldGreen "${Esc}[1;32m")
    set(BoldYellow "${Esc}[1;33m")
    set(BoldBlue "${Esc}[1;34m")
    set(BoldMagenta "${Esc}[1;35m")
    set(BoldCyan "${Esc}[1;36m")
    set(BoldWhite "${Esc}[1;37m")
endif()

# Detect CPU cores for parallel builds
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    execute_process(COMMAND sysctl -n hw.logicalcpu OUTPUT_VARIABLE CPU_CORES OUTPUT_STRIP_TRAILING_WHITESPACE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
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
message(STATUS "Parallel build jobs: ${CPU_CORES}")

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
# - .deps-cache/<BuildType>/      : Normal glibc/system libc builds (per build type)
# - .deps-cache-musl/<BuildType>/ : musl libc builds (different ABI, per build type)
#
# Build types need separate caches because dependencies like mimalloc have different
# configurations (Debug: MI_DEBUG_FULL=ON, Release: MI_DEBUG_FULL=OFF)
#
# To force rebuild dependencies: rm -rf .deps-cache*

# Determine cache directory based on build configuration and build type
# Allow override via DEPS_CACHE_BASE environment variable (useful for Docker)
# Note: USE_MUSL may not be set yet, so we check if it's defined and ON
if(DEFINED ENV{DEPS_CACHE_BASE})
    set(DEPS_CACHE_BASE_DIR "$ENV{DEPS_CACHE_BASE}")
    message(STATUS "Using custom dependency cache base from environment: ${DEPS_CACHE_BASE_DIR}")
elseif(DEFINED USE_MUSL AND USE_MUSL)
    set(DEPS_CACHE_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps-cache-musl")
else()
    set(DEPS_CACHE_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps-cache")
endif()

# For musl, don't use per-build-type subdirectories since dependencies are static libs
# For non-musl, use build-type-specific directories for mimalloc debug/release builds
# Note: USE_MUSL may not be set yet, so we check if it's defined and ON
if(DEFINED USE_MUSL AND USE_MUSL)
    set(FETCHCONTENT_BASE_DIR "${DEPS_CACHE_BASE_DIR}" CACHE PATH "FetchContent cache directory")
else()
    set(FETCHCONTENT_BASE_DIR "${DEPS_CACHE_BASE_DIR}/${CMAKE_BUILD_TYPE}" CACHE PATH "FetchContent cache directory")
endif()
message(STATUS "Using dependency cache: ${FETCHCONTENT_BASE_DIR}")

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

# =============================================================================
# Build System Generator Configuration
# =============================================================================

# Use Ninja generator by default on all platforms for faster builds
# Only set Ninja if no generator was explicitly specified via -G flag
if(NOT CMAKE_GENERATOR AND NOT DEFINED CMAKE_GENERATOR_INTERNAL)
    find_program(NINJA_EXECUTABLE ninja)
    if(NINJA_EXECUTABLE)
        set(CMAKE_GENERATOR "Ninja" CACHE STRING "Build system generator" FORCE)
        message(STATUS "Using Ninja generator for faster builds")
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

