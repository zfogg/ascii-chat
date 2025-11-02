# =============================================================================
# Compiler Flags Configuration Module
# =============================================================================
# This module provides platform-specific compiler flags and debug mode setup

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
#   BUILD_TYPE - "Debug" or "Dev"
function(configure_debug_build_flags BUILD_TYPE)
    add_compile_options(-g -O0 -DDEBUG)

    # Windows-specific debug info formats
    if(WIN32)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            if(BUILD_TYPE STREQUAL "Debug")
                # CodeView debug format for Debug mode
                add_compile_options(-gcodeview)
            else()
                # Dev mode: Use -g2 for full debug info and ensure PDB generation
                add_compile_options(-g2 -gcodeview)
                add_link_options(-Wl,/DEBUG:FULL)
                message(STATUS "Dev build: Enabled PDB generation with full debug info")
            endif()
        elseif(MSVC)
            # For MSVC, explicitly enable PDB generation
            add_compile_options(/Zi)
            add_link_options(/DEBUG:FULL)
            message(STATUS "${BUILD_TYPE} build: Enabled MSVC PDB generation")
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
function(configure_release_flags PLATFORM_DARWIN PLATFORM_LINUX IS_ROSETTA IS_APPLE_SILICON ENABLE_CRC32_HW)
    add_definitions(-DNDEBUG)

    # Remove absolute file paths from __FILE__ macro expansions
    # This prevents usernames and full paths from appearing in release binaries
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
        # Get the source directory and normalize paths for Windows
        get_filename_component(SOURCE_DIR "${CMAKE_SOURCE_DIR}" ABSOLUTE)

        # On Windows, convert backslashes to forward slashes (required by macro-prefix-map)
        if(WIN32)
            string(REPLACE "\\" "/" SOURCE_DIR_NORMALIZED "${SOURCE_DIR}")
            set(SOURCE_DIR "${SOURCE_DIR_NORMALIZED}")
        endif()

        # Map source directory to empty string (removes absolute path, keeps relative path)
        add_compile_options(-fmacro-prefix-map="${SOURCE_DIR}/=")
    elseif(MSVC AND CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Clang-cl (MSVC-compatible frontend) supports -fmacro-prefix-map
        get_filename_component(SOURCE_DIR "${CMAKE_SOURCE_DIR}" ABSOLUTE)
        string(REPLACE "\\" "/" SOURCE_DIR_NORMALIZED "${SOURCE_DIR}")
        add_compile_options(-fmacro-prefix-map="${SOURCE_DIR_NORMALIZED}/=")
    endif()

    # CPU-aware optimization flags
    add_compile_options(-O3 -funroll-loops -fstrict-aliasing -ftree-vectorize -fomit-frame-pointer -pipe)

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
