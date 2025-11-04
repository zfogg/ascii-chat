# =============================================================================
# Build Configuration Module
# =============================================================================
# This module configures build settings: terminal colors, CPU cores, C standard,
# build types, and output directories
#
# Prerequisites:
#   - Must run after project()
#   - CMAKE_SYSTEM_NAME must be set
#
# Outputs:
#   - Color* variables for terminal output
#   - CPU_CORES for parallel builds
#   - CMAKE_C_STANDARD set to 23
#   - CMAKE_*_OUTPUT_DIRECTORY set
# =============================================================================

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

# C standard selection - intelligently detect the best available standard
# Uses modern CMake compiler feature detection instead of manual flag checking
# Only set C23 for GNU/Clang compilers
if(NOT CMAKE_C_STANDARD)
    # Only configure C23 for Clang and GCC compilers
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
        # Set C23 standard - CMake will automatically handle fallback to C2X if needed
        # CMAKE_C_STANDARD_REQUIRED OFF allows CMake to gracefully fall back
        set(CMAKE_C_STANDARD 23)
        set(CMAKE_C_STANDARD_REQUIRED OFF)  # Allow fallback to C2X if C23 not available

        # Try strict C23 without GNU extensions
        set(CMAKE_C_EXTENSIONS OFF)

        # Verify the standard was set (CMake will warn if it falls back)
        message(STATUS "Using C23 standard (will fall back to C2X if not supported)")
    else()
        # Non-Clang/GCC compilers: use C17 (widely supported)
        set(CMAKE_C_STANDARD 17)
        set(CMAKE_C_STANDARD_REQUIRED ON)
        set(CMAKE_C_EXTENSIONS ON)
        message(STATUS "Using C17 standard for ${CMAKE_C_COMPILER_ID} compiler")
    endif()
endif()

# Note: CMAKE_C_STANDARD_REQUIRED is set per-compiler above
# This ensures we get the best available standard with graceful fallback

# Option to build tests
option(BUILD_TESTS "Build test executables" ON)

# Build type (matches Makefile modes)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type: Debug, Dev, Release, Coverage" FORCE)
endif()

# Valid build types (matching Makefile)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Dev" "Release" "Coverage" "TSan")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

