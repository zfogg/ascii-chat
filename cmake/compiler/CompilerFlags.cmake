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

    # Essential safety warnings (always enabled)
    # Keep it simple - just the important stuff that catches real bugs
    add_compile_options(
        -Wformat=2                # Extra format string security checks
        -Wwrite-strings           # Warn when string literals could be written to
        -Wnull-dereference        # Warn about potential null pointer dereferences
        -Wformat-security         # Warn about potential format string vulnerabilities
    )

    # Clang-specific warnings
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        add_compile_options(
            -Wconditional-uninitialized  # Warn about potentially uninitialized variables
            -Widiomatic-parentheses   # Warn about confusing precedence without parentheses
        )

        # Disable noisy Clang warnings
        add_compile_options(
            -Wno-comma                                       # Too noisy with comma operator in macros/third-party
            -Wno-format-nonliteral                           # False positives in variadic format functions
            -Wno-incompatible-pointer-types-discards-qualifiers  # Pragmatic const handling
        )
    endif()

    # GCC-specific warnings
    if(CMAKE_C_COMPILER_ID MATCHES "GNU")
        add_compile_options(
            -Wlogical-op              # Warn about suspicious logical operations
            -Wduplicated-cond         # Warn about duplicated if-else conditions
            -Wduplicated-branches     # Warn about identical if/else branches
            -Wtrampolines             # Warn about trampolines (nested functions)
        )
    endif()

    # Enable frame pointers for better backtraces (required for musl + libexecinfo)
    # Frame pointers help with stack traces and debugging, but disable in Release for performance
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-fno-omit-frame-pointer)
        message(STATUS "${BoldGreen}Frame pointers${ColorReset} enabled for backtraces")
    endif()
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
        message(STATUS "DEBUG_MEMORY disabled - ${BoldBlue}AddressSanitizer${ColorReset} provides comprehensive memory checking (conflicts with mutex init)")
    elseif(USE_MIMALLOC_ARG)
        message(STATUS "DEBUG_MEMORY disabled - using ${BoldBlue}mimalloc${ColorReset}'s memory tracking instead")
        # Enable mimalloc debugging features (MI_DEBUG will be set per-target to avoid conflicts)
        add_definitions(-DUSE_MIMALLOC_DEBUG)
        set(MIMALLOC_DEBUG_LEVEL 2 PARENT_SCOPE)  # MI_DEBUG level 2: basic checks + internal assertions
    elseif(USE_MUSL_ARG)
        message(STATUS "${BoldYellow}DEBUG_MEMORY${ColorReset} disabled - ${BoldBlue}musl${ColorReset}'s strict aliasing is incompatible with DEBUG_MEMORY macros")
    else()
        # Enable DEBUG_MEMORY only when no conflicts exist
        add_definitions(-DDEBUG_MEMORY)
        message(STATUS "${BoldGreen}DEBUG_MEMORY${ColorReset} enabled for detailed memory leak tracking")
    endif()
endfunction()

# Configure debug build flags (Debug or Dev mode)
# Args:
#   BUILD_TYPE - "Debug", "Dev", or "Sanitize"
function(configure_debug_build_flags BUILD_TYPE)
    # Enhanced debug info with macro definitions
    add_compile_options(-g3 -O0 -DDEBUG)

    # Better debugging experience
    add_compile_options(
        -fno-inline                           # Disable inlining for easier stepping
        -fno-eliminate-unused-debug-types     # Keep all debug types
    )

    # Clang-specific debug enhancements
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        add_compile_options(
            -fstandalone-debug                # Full debug info even for headers
            -gcolumn-info                     # Include column information
        )
    endif()

    # Stack protection in debug builds (helps catch buffer overflows early)
    add_compile_options(-fstack-protector-strong)

    # Use LLD linker for faster link times in Debug/Dev builds
    # LLD is 2-3x faster than default linker (ld64 on macOS, GNU ld on Linux)
    # Only for Debug/Dev - Release uses default linker for maximum stability
    if(NOT WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
        add_link_options(-fuse-ld=lld)
        message(STATUS "Using ${BoldCyan}LLD linker${ColorReset} for faster Debug/Dev builds")
    endif()

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
        # "cmake/utils/remove_paths.sh" (via Git Bash or WSL) that edits strings with paths from the
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

        # Additional aggressive optimizations for Release builds (cross-platform)
        add_compile_options(
            -fno-math-errno              # Don't set errno for math functions (faster)
            -fno-signed-zeros            # Allow optimizations that ignore sign of zero
            -freciprocal-math            # Allow reciprocal approximations
            -fassociative-math           # Allow reassociation of FP operations
            -fno-trapping-math           # Assume no floating-point exceptions
            -funsafe-math-optimizations  # Allow unsafe math optimizations (enables several flags at once)
        )

        # Additional safe optimizations
        add_compile_options(
            -foptimize-sibling-calls     # Optimize tail recursion
        )

        # GCC-specific optimizations (GCC has more aggressive tree/loop opts than Clang)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU")
            add_compile_options(
                -ftree-loop-distribution     # Loop distribution optimization
                -fgcse-after-reload          # Global common subexpression elimination after reload
                -fpredictive-commoning       # Predictive commoning optimization
                -fsplit-loops                # Loop splitting
                -funswitch-loops             # Loop unswitching
                -ftree-loop-im               # Loop invariant motion
                -fivopts                     # Induction variable optimizations
                -ftree-partial-pre           # Partial redundancy elimination on trees
                -fipa-pta                    # Interprocedural pointer analysis
            )
        endif()

        # Clang-specific optimizations
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            # Check if Polly is available before using it
            include(CheckCCompilerFlag)
            check_c_compiler_flag("-mllvm -polly" COMPILER_SUPPORTS_POLLY)

            if(COMPILER_SUPPORTS_POLLY)
                add_compile_options(-mllvm -polly)  # Enable Polly loop optimizer
            endif()

            # Additional Clang optimizations
            # Only use -fno-semantic-interposition on Linux
            # - macOS: Not supported (causes warning with Homebrew clang)
            # - Windows: Unused (not a shared library platform for this flag)
            if(PLATFORM_LINUX)
                add_compile_options(-fno-semantic-interposition)  # Allow inlining across DSO boundaries
            endif()
            # Note: -fvisibility=hidden is applied per-target in Executables.cmake (not globally)
            # This allows shared libraries to use -fvisibility=default while executables use hidden
        endif()
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
    elseif(WIN32)
        # Windows: Use native CPU optimizations
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|X86_64")
            add_compile_options(-march=native -ffp-contract=fast -ffinite-math-only)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            add_compile_options(-mcpu=native -ffp-contract=fast -ffinite-math-only)
        else()
            add_compile_options(-ffp-contract=fast)
        endif()
    else()
        add_compile_options(-ffp-contract=fast)
    endif()

    # Link-time optimization is applied per-target in Executables.cmake
    # Not applied globally to allow shared libraries to export all symbols
    # (LTO strips "unused" symbols which are actually needed by external users)

    # =============================================================================
    # Dead Code Elimination (all platforms)
    # =============================================================================
    # Requires -ffunction-sections -fdata-sections to separate each function/data into sections
    add_compile_options(-ffunction-sections -fdata-sections)

    if(PLATFORM_LINUX)
        # GNU ld: Use --gc-sections to remove unused sections
        add_link_options(-Wl,--gc-sections)
    elseif(APPLE)
        # Apple ld: Use -dead_strip to remove unused sections
        add_link_options(-Wl,-dead_strip)
    elseif(WIN32)
        # MSVC linker: Use /OPT:REF and /OPT:ICF for dead code elimination and COMDAT folding
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            add_link_options(-Wl,/OPT:REF -Wl,/OPT:ICF)
        endif()
    endif()

    # =============================================================================
    # Size reduction and alignment optimizations (platform-specific)
    # =============================================================================
    if(NOT WIN32)
        # Unix: Disable stack protector and unwind tables for smaller binaries
        add_compile_options(-fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables)
    else()
        # Windows: Can't disable unwind tables (required for SEH exception handling)
    endif()

    # All platforms: Math and alignment optimizations
    add_compile_options(-fno-trapping-math -falign-loops=32 -falign-functions=32 -fmerge-all-constants)

    # Security hardening for Unix-like platforms
    if(NOT WIN32)
        if(NOT WITH_DEBUG_INFO)
            # Position Independent Executable (PIE) - better ASLR
            add_compile_options(-fPIE)

            if(USE_MUSL)
                # Static-PIE for musl builds (combines static linking with ASLR)
                # Note: Static-PIE uses -static-pie flag (set in Musl.cmake linker flags)
                #       and rcrt1.o startup file instead of regular -pie + crt1.o
            else()
                # Dynamic PIE for regular builds
                # Note: Windows is excluded by parent if(NOT WIN32) at line 318
                if(PLATFORM_LINUX)
                    add_link_options(LINKER:-pie)
                    # Relocation hardening (Linux/ELF only - not supported on macOS Mach-O)
                    add_link_options(-Wl,-z,relro -Wl,-z,now)
                elseif(APPLE)
                    add_link_options(-Wl,-pie)
                endif()
            endif()
        endif()

        # Control Flow Integrity on supported platforms (x86_64 with CET support)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            # Intel CET (Control-flow Enforcement Technology)
            if(CMAKE_C_COMPILER_ID MATCHES "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "8.0")
                add_compile_options(-fcf-protection=full)
            elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "7.0")
                add_compile_options(-fcf-protection=full)
            endif()
        endif()

        # Linux-specific flags (not supported on macOS clang, Windows already excluded by parent if(NOT WIN32))
        if(PLATFORM_LINUX)
            # Stack clash protection (GCC 8+, Clang 11+) - not supported on macOS (causes warning)
            add_compile_options(-fstack-clash-protection)
            add_compile_options(-fno-plt -fno-semantic-interposition)
            add_link_options(-fno-plt)

            # Fortify source for additional runtime checks (not compatible with musl)
            if(NOT USE_MUSL)
                add_definitions(-D_FORTIFY_SOURCE=3)
            else()
                # Musl doesn't provide __*_chk fortify wrappers
                add_definitions(-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
            endif()
        endif()

        # macOS-specific security features
        if(PLATFORM_DARWIN)
            # Fortify source (level 2 is safer for macOS)
            add_definitions(-D_FORTIFY_SOURCE=2)
        endif()
    endif()
endfunction()

# Configure coverage build flags
function(configure_coverage_flags)
    add_definitions(-DDEBUG_MEMORY -DCOVERAGE_BUILD)
    add_compile_options(-g -O0 --coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
endfunction()
