# =============================================================================
# Compiler Flags Configuration Module
# =============================================================================
# This module provides platform-specific compiler flags and debug mode setup

# Configure DEBUG_MEMORY based on mimalloc and musl settings
# Args:
#   USE_MIMALLOC_ARG - Whether mimalloc is enabled
#   USE_MUSL_ARG - Whether musl libc is enabled
function(configure_debug_memory USE_MIMALLOC_ARG USE_MUSL_ARG)
    # Don't add DEBUG_MEMORY if mimalloc is enabled - mimalloc provides its own memory tracking
    # Don't add DEBUG_MEMORY if musl is enabled - musl's strict aliasing breaks the macros
    if(NOT USE_MIMALLOC_ARG AND NOT USE_MUSL_ARG)
        add_definitions(-DDEBUG_MEMORY)
    elseif(USE_MIMALLOC_ARG)
        message(STATUS "DEBUG_MEMORY disabled - using mimalloc's memory tracking instead")
        # Enable mimalloc debugging features (MI_DEBUG will be set per-target to avoid conflicts)
        add_definitions(-DUSE_MIMALLOC_DEBUG)
        set(MIMALLOC_DEBUG_LEVEL 2 PARENT_SCOPE)  # MI_DEBUG level 2: basic checks + internal assertions
    elseif(USE_MUSL_ARG)
        message(STATUS "DEBUG_MEMORY disabled - musl's strict aliasing is incompatible with DEBUG_MEMORY macros")
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
        add_compile_options(-flto -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-trapping-math -falign-loops=32 -falign-functions=32 -fno-plt -fno-semantic-interposition -fmerge-all-constants)
        add_link_options(-flto -fno-plt)
    endif()
endfunction()

# Configure coverage build flags
function(configure_coverage_flags)
    add_definitions(-DDEBUG_MEMORY -DCOVERAGE_BUILD)
    add_compile_options(-g -O0 --coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
endfunction()
