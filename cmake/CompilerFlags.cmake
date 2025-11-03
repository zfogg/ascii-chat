# =============================================================================
# Compiler Flags Configuration Module
# =============================================================================
# This module provides platform-specific compiler flags and debug mode setup
#
# Functions:
#   - configure_base_compiler_flags(): Sets base warning flags and frame pointer options
#   - configure_debug_memory(): Configures DEBUG_MEMORY based on mimalloc, musl, sanitizers
#   - configure_debug_build_flags(): Sets debug build flags
#   - configure_release_flags(): Sets release build optimization flags
#   - configure_coverage_flags(): Sets coverage build flags
#
# Prerequisites:
#   - configure_base_compiler_flags() should be called after project() but before build type config
#   - Other functions called based on build type
# =============================================================================

# =============================================================================
# Base Compiler Flags
# =============================================================================
# Configure base warning flags and frame pointer options that apply to all builds
#
# Prerequisites:
#   - Must run after project()
# =============================================================================

function(configure_base_compiler_flags)
    # Base warning flags for Clang/GCC compilers
    # -Wall: Enable common warnings
    # -Wextra: Enable extra warnings
    add_compile_options(-Wall -Wextra)

    # Enable frame pointers for better backtraces (required for musl + libexecinfo)
    # Frame pointers help with stack traces and debugging, but disable in Release for performance
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-fno-omit-frame-pointer)
        message(STATUS "Frame pointers enabled for backtraces")
    endif()

    # =============================================================================
    # Future compiler flags can be added here
    # =============================================================================
    # Examples:
    #   - Additional warning flags: -Wpedantic, -Werror, etc.
    #   - Language standard enforcement: -Wconversion, -Wsign-conversion
    #   - Platform-specific flags
    # =============================================================================
endfunction()

# =============================================================================
# DEBUG_MEMORY Configuration
# =============================================================================
# Configure DEBUG_MEMORY based on mimalloc, musl, and sanitizer settings
# Args:
#   USE_MIMALLOC_ARG - Whether mimalloc is enabled
#   USE_MUSL_ARG - Whether musl libc is enabled
#   USE_SANITIZERS_ARG - Whether AddressSanitizer will be enabled (optional, defaults to false)
function(configure_debug_memory USE_MIMALLOC_ARG USE_MUSL_ARG)
    # Handle optional USE_SANITIZERS_ARG parameter
    set(USE_SANITIZERS_ARG FALSE)
    if(ARGC GREATER 2)
        set(USE_SANITIZERS_ARG ${ARGV2})
    endif()

    # Don't add DEBUG_MEMORY if explicitly disabled
    if(DEFINED DEBUG_MEMORY AND NOT DEBUG_MEMORY)
        message(STATUS "DEBUG_MEMORY disabled by user")
        return()
    endif()

    # Don't add DEBUG_MEMORY if mimalloc is enabled - mimalloc provides its own memory tracking
    # Don't add DEBUG_MEMORY if musl is enabled - musl's strict aliasing breaks the macros
    # Don't add DEBUG_MEMORY if sanitizers are enabled - ASan conflicts with mutex initialization during static init
    if(USE_SANITIZERS_ARG)
        message(STATUS "DEBUG_MEMORY disabled - AddressSanitizer provides comprehensive memory checking (conflicts with mutex init)")
    elseif(USE_MIMALLOC_ARG)
        message(STATUS "DEBUG_MEMORY disabled - using mimalloc's memory tracking instead")
        # Enable mimalloc debugging features (MI_DEBUG will be set per-target to avoid conflicts)
        add_definitions(-DUSE_MIMALLOC_DEBUG)
        set(MIMALLOC_DEBUG_LEVEL 2 PARENT_SCOPE)  # MI_DEBUG level 2: basic checks + internal assertions
    elseif(USE_MUSL_ARG)
        message(STATUS "DEBUG_MEMORY disabled - musl's strict aliasing is incompatible with DEBUG_MEMORY macros")
    else()
        # Enable DEBUG_MEMORY only when no conflicts exist
        add_definitions(-DDEBUG_MEMORY)
        message(STATUS "DEBUG_MEMORY enabled for detailed memory leak tracking")
    endif()
endfunction()

# Configure debug build flags (Debug or Dev mode)
# Args:
#   BUILD_TYPE - "Debug", "Dev", or "Sanitize"
function(configure_debug_build_flags BUILD_TYPE)
    add_compile_options(-g -O0 -DDEBUG)

    # Windows-specific debug info formats
    if(WIN32)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            if(BUILD_TYPE STREQUAL "Debug")
                # CodeView debug format for Debug mode
                add_compile_options(-gcodeview)
            elseif(BUILD_TYPE STREQUAL "Dev" OR BUILD_TYPE STREQUAL "Sanitize")
                # Dev mode: Use -g2 for full debug info and ensure PDB generation
                add_compile_options(-g2 -gcodeview)
                add_link_options(-Wl,/DEBUG:FULL)
                message(STATUS "Dev build: Enabled PDB generation with full debug info")
            endif()
        endif()
    endif()
endfunction()

# Configure release build optimization flags
# Args:
#   PLATFORM_DARWIN - Whether platform is macOS
#   PLATFORM_LINUX - Whether platform is Linux
#   IS_ROSETTA - Whether running under Rosetta
#   IS_APPLE_SILICON - Whether running on Apple Silicon
#   ENABLE_CRC32_HW - Whether CRC32 hardware acceleration is enabled
#   WITH_DEBUG_INFO - Optional: If true, keep debug symbols (for RelWithDebInfo), default false
function(configure_release_flags PLATFORM_DARWIN PLATFORM_LINUX IS_ROSETTA IS_APPLE_SILICON ENABLE_CRC32_HW)
    # Handle optional WITH_DEBUG_INFO parameter
    set(WITH_DEBUG_INFO FALSE)
    if(ARGC GREATER 5)
        set(WITH_DEBUG_INFO ${ARGV5})
    endif()

    add_definitions(-DNDEBUG)

    # Disable debug info generation in release builds to prevent path embedding
    # This ensures no debug symbols or paths are embedded in release binaries
    # But keep debug info for RelWithDebInfo builds
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
        if(WITH_DEBUG_INFO)
            # RelWithDebInfo: Keep debug symbols but still optimize
            add_compile_options(-g)
            message(STATUS "Release build with debug info: keeping debug symbols")
        else()
            # Release: No debug info
            add_compile_options(-g0)
        endif()
    endif()

    # Remove absolute file paths from __FILE__ macro expansions
    # This prevents usernames and full paths from appearing in release binaries
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
        # Get the source directory and normalize paths for Windows
        get_filename_component(SOURCE_DIR "${CMAKE_SOURCE_DIR}" ABSOLUTE)

        # NOTE: On Windows, this doesn't work. We use the bash script
        # "cmake/remove_paths.sh" (via Git Bash or WSL) that edits strings with paths from the
        # builder's machine in them found in the release binary for after it's
        # built.
        add_compile_options(-fmacro-prefix-map="${SOURCE_DIR}/=")
    endif()

    # CPU-aware optimization flags
    # For RelWithDebInfo, use -O2 instead of -O3 to maintain better debug info quality
    if(WITH_DEBUG_INFO)
        add_compile_options(-O2 -funroll-loops -fstrict-aliasing -ftree-vectorize -pipe)
        # Keep frame pointers in RelWithDebInfo for better backtraces
        add_compile_options(-fno-omit-frame-pointer)
    else()
        add_compile_options(-O3 -funroll-loops -fstrict-aliasing -ftree-vectorize -fomit-frame-pointer -pipe)
    endif()

    if(PLATFORM_DARWIN)
        if(IS_ROSETTA EQUAL 1)
            add_compile_options(-march=native -ffp-contract=fast -ffinite-math-only)
        elseif(IS_APPLE_SILICON EQUAL 1)
            # Apple Silicon: add +crc if CRC32 hardware is enabled
            if(ENABLE_CRC32_HW)
                add_compile_options(-march=armv8-a+crc -mcpu=native -ffast-math -ffp-contract=fast)
            else()
                add_compile_options(-march=native -mcpu=native -ffast-math -ffp-contract=fast)
            endif()
        else()
            add_compile_options(-march=native -ffp-contract=fast -ffinite-math-only)
        endif()
    elseif(PLATFORM_LINUX)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
            add_compile_options(-mcpu=native -ffp-contract=fast -ffinite-math-only)
        else()
            add_compile_options(-march=native -ffp-contract=fast -ffinite-math-only)
        endif()
    else()
        add_compile_options(-ffp-contract=fast)
    endif()

    if(NOT WIN32)
        add_compile_options(-flto -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-trapping-math -falign-loops=32 -falign-functions=32 -fmerge-all-constants)
        add_link_options(-flto)
        # Linux-specific flags (not supported on macOS clang)
        if(PLATFORM_LINUX)
            add_compile_options(-fno-plt -fno-semantic-interposition)
            add_link_options(-fno-plt)
        endif()
    endif()
endfunction()

# Configure coverage build flags
function(configure_coverage_flags)
    add_definitions(-DDEBUG_MEMORY -DCOVERAGE_BUILD)
    add_compile_options(-g -O0 --coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
endfunction()
