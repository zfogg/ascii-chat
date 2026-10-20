# =============================================================================
# Compiler Flags Configuration Module
# =============================================================================
# This module provides platform-specific compiler flags and debug mode setup
#
# Functions:
#   - configure_base_compiler_flags(): Sets base warning flags and frame pointer options
#   - configure_lld_linker(): Configures LLD linker for all build types
#   - configure_debug_memory(): Configures DEBUG_MEMORY based on mimalloc, musl, sanitizers
#   - configure_debug_build_flags(): Sets debug build flags
#   - configure_release_flags(): Sets release build optimization flags
#   - configure_coverage_flags(): Sets coverage build flags
#
# Prerequisites:
#   - configure_base_compiler_flags() should be called after project() but before build type config
#   - Other functions called based on build type
# =============================================================================

# Include CMake modules for compiler/linker flag checking
include(CheckCCompilerFlag)
include(${CMAKE_CURRENT_LIST_DIR}/SourcePaths.cmake)
include(CheckLinkerFlag)
include(CheckPIESupported)
include(CheckCSourceRuns)

function(configure_toolchain_capabilities)
    if(DEFINED ASCIICHAT_TOOLCHAIN_CHECKS_CONFIGURED)
        return()
    endif()

    check_c_compiler_flag("-fstack-protector-strong" ASCIICHAT_SUPPORTS_STACK_PROTECTOR_STRONG)
    check_c_compiler_flag("-fstack-clash-protection" ASCIICHAT_SUPPORTS_STACK_CLASH_PROTECTION)
    check_c_compiler_flag("-fcf-protection=full" ASCIICHAT_SUPPORTS_CFI_FULL)
    check_c_compiler_flag("-fno-omit-frame-pointer" ASCIICHAT_SUPPORTS_NO_OMIT_FRAME_POINTER)

    if(WIN32)
        check_c_compiler_flag("/Qspectre" ASCIICHAT_SUPPORTS_SPECTRE_MITIGATION)
        check_linker_flag(C "LINKER:/guard:cf" ASCIICHAT_SUPPORTS_GUARD_CF)
        check_linker_flag(C "LINKER:/dynamicbase" ASCIICHAT_SUPPORTS_DYNAMICBASE)
        check_linker_flag(C "LINKER:/nxcompat" ASCIICHAT_SUPPORTS_NXCOMPAT)
        check_linker_flag(C "LINKER:/highentropyva" ASCIICHAT_SUPPORTS_HIGHENTROPY_VA)
        check_linker_flag(C "LINKER:/Brepro" ASCIICHAT_SUPPORTS_BREPRO)
    elseif(APPLE)
        check_linker_flag(C "-Wl,-dead_strip_dylibs" ASCIICHAT_SUPPORTS_DEAD_STRIP_DYLIBS)
    elseif(UNIX)
        # Force executable for linker flag tests (RELRO/NOW don't apply to static libraries)
        set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
        check_linker_flag(C "-Wl,-z,pack-relative-relocs" ASCIICHAT_SUPPORTS_PACK_REL_RELOCS)
        check_linker_flag(C "-Wl,-z,relro" ASCIICHAT_SUPPORTS_RELRO)
        check_linker_flag(C "-Wl,-z,now" ASCIICHAT_SUPPORTS_NOW)
        unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
    endif()

    set(ASCIICHAT_TOOLCHAIN_CHECKS_CONFIGURED TRUE CACHE INTERNAL "Toolchain checks already evaluated")

    mark_as_advanced(
        ASCIICHAT_SUPPORTS_STACK_PROTECTOR_STRONG
        ASCIICHAT_SUPPORTS_STACK_CLASH_PROTECTION
        ASCIICHAT_SUPPORTS_CFI_FULL
        ASCIICHAT_SUPPORTS_NO_OMIT_FRAME_POINTER
        ASCIICHAT_SUPPORTS_SPECTRE_MITIGATION
        ASCIICHAT_SUPPORTS_GUARD_CF
        ASCIICHAT_SUPPORTS_DYNAMICBASE
        ASCIICHAT_SUPPORTS_NXCOMPAT
        ASCIICHAT_SUPPORTS_HIGHENTROPY_VA
        ASCIICHAT_SUPPORTS_BREPRO
        ASCIICHAT_SUPPORTS_DEAD_STRIP_DYLIBS
        ASCIICHAT_SUPPORTS_PACK_REL_RELOCS
        ASCIICHAT_SUPPORTS_RELRO
        ASCIICHAT_SUPPORTS_NOW
    )
endfunction()

# =============================================================================
# Helper Functions for Safe Flag Addition
# =============================================================================

# Check and add compiler flag if supported
function(add_compiler_flag_if_supported flag)
    string(MAKE_C_IDENTIFIER "HAVE_CFLAG_${flag}" flag_var)
    check_c_compiler_flag("${flag}" ${flag_var})
    if(${flag_var})
        target_compile_options(asciichat-project-options INTERFACE ${flag})
    endif()
endfunction()

# Check and add linker flag if supported
function(add_linker_flag_if_supported flag)
    string(MAKE_C_IDENTIFIER "HAVE_LFLAG_${flag}" flag_var)
    check_linker_flag(C "${flag}" ${flag_var})
    if(${flag_var})
        target_link_options(asciichat-project-options INTERFACE ${flag})
    endif()
endfunction()

# =============================================================================
# Base Compiler Flags
# =============================================================================
# Configure base warning flags and frame pointer options that apply to all builds
#
# Prerequisites:
#   - Must run after project()
# =============================================================================

function(configure_base_compiler_flags)
    configure_toolchain_capabilities()

    # If CLANG_RESOURCE_DIR is set (when building LibTooling tools like defer),
    # pass it to the compiler so clang can find its builtin headers
    if(DEFINED CLANG_RESOURCE_DIR AND CLANG_RESOURCE_DIR)
        target_compile_options(asciichat-project-options INTERFACE -resource-dir "${CLANG_RESOURCE_DIR}")
    endif()

    # Base warning flags for Clang/GCC compilers
    # -Wall: Enable common warnings
    # -Wextra: Enable extra warnings
    target_compile_options(asciichat-project-options INTERFACE -Wall -Wextra)

    # Essential safety warnings (always enabled)
    # Keep it simple - just the important stuff that catches real bugs
    target_compile_options(asciichat-project-options INTERFACE
        -Wformat=2                # Extra format string security checks
        -Wwrite-strings           # Warn when string literals could be written to
        -Wnull-dereference        # Warn about potential null pointer dereferences
        -Wformat-security         # Warn about potential format string vulnerabilities
    )

    # Clang-specific warnings
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(asciichat-project-options INTERFACE
            -Wconditional-uninitialized  # Warn about potentially uninitialized variables
            -Widiomatic-parentheses   # Warn about confusing precedence without parentheses
        )

        # Disable noisy Clang warnings
        target_compile_options(asciichat-project-options INTERFACE
            -Wno-comma                                       # Too noisy with comma operator in macros/third-party
            -Wno-format-nonliteral                           # False positives in variadic format functions
            -Wno-incompatible-pointer-types-discards-qualifiers  # Pragmatic const handling
        )
    endif()

    # GCC-specific warnings
    if(CMAKE_C_COMPILER_ID MATCHES "GNU")
        target_compile_options(asciichat-project-options INTERFACE
            -Wlogical-op              # Warn about suspicious logical operations
            -Wduplicated-cond         # Warn about duplicated if-else conditions
            -Wduplicated-branches     # Warn about identical if/else branches
            -Wtrampolines             # Warn about trampolines (nested functions)
        )
    endif()

    # Enable frame pointers for better backtraces (required for musl + libexecinfo)
    # Frame pointers help with stack traces and debugging, but disable in Release for performance
    if(ASCIICHAT_SUPPORTS_NO_OMIT_FRAME_POINTER AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(asciichat-project-options INTERFACE -fno-omit-frame-pointer)
        message(STATUS "${BoldGreen}Frame pointers${ColorReset} enabled for backtraces")
    endif()
endfunction()

# =============================================================================
# LLD Linker Configuration (all build types)
# =============================================================================
# Use LLD linker for all builds — required for -Wl,-mllvm flags (ThinLTO, PIC)
# and 2-3x faster than GNU ld / ld64.
function(configure_lld_linker)
    if(NOT WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
        if(ASCIICHAT_LLD_EXECUTABLE AND EXISTS "${ASCIICHAT_LLD_EXECUTABLE}")
            execute_process(
                COMMAND "${ASCIICHAT_LLD_EXECUTABLE}" --version
                RESULT_VARIABLE LLD_CHECK_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(LLD_CHECK_RESULT EQUAL 0)
                target_link_options(asciichat-project-options INTERFACE -fuse-ld=lld)
                if(PLATFORM_LINUX)
                    target_link_options(asciichat-project-options INTERFACE "LINKER:--allow-multiple-definition")
                endif()
                message(STATUS "Using ${BoldCyan}LLD linker${ColorReset} (${ASCIICHAT_LLD_EXECUTABLE})")
            else()
                message(STATUS "${Yellow}LLD linker found but not functional${ColorReset} - using default linker")
            endif()
        else()
            message(STATUS "${Yellow}LLD linker not found${ColorReset} - using default linker")
        endif()
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
        target_compile_definitions(asciichat-project-options INTERFACE USE_MIMALLOC_DEBUG)
        set(MIMALLOC_DEBUG_LEVEL 2 PARENT_SCOPE)  # MI_DEBUG level 2: basic checks + internal assertions
    elseif(USE_MUSL_ARG)
        message(STATUS "${BoldYellow}DEBUG_MEMORY${ColorReset} disabled - ${BoldBlue}musl${ColorReset}'s strict aliasing is incompatible with DEBUG_MEMORY macros")
    else()
        # Enable DEBUG_MEMORY only when no conflicts exist
        target_compile_definitions(asciichat-project-options INTERFACE DEBUG_MEMORY)
        message(STATUS "${BoldGreen}DEBUG_MEMORY${ColorReset} enabled for detailed memory leak tracking")
    endif()
endfunction()

# Configure debug build flags (Debug or Dev mode)
# Args:
#   BUILD_TYPE - "Debug", "Dev", or "Sanitize"
function(configure_debug_build_flags BUILD_TYPE)
    # Enhanced debug info with macro definitions
    target_compile_options(asciichat-project-options INTERFACE -g3 -O0 -DDEBUG)

    # Better debugging experience
    target_compile_options(asciichat-project-options INTERFACE
        -fno-inline                           # Disable inlining for easier stepping
        -fno-eliminate-unused-debug-types     # Keep all debug types
    )

    # Clang-specific debug enhancements
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(asciichat-project-options INTERFACE
            -fstandalone-debug                # Full debug info even for headers
            -gcolumn-info                     # Include column information
        )
    endif()

    # Stack protection in debug builds (helps catch buffer overflows early)
    target_compile_options(asciichat-project-options INTERFACE -fstack-protector-strong)

    # LLD linker setup moved to configure_lld_linker() — called for all build types

    # Windows-specific debug info formats
    if(WIN32)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            # Let LLD merge CodeView type records using compiler-provided hashes.
            check_c_compiler_flag("-gcodeview-ghash" ASCIICHAT_SUPPORTS_CODEVIEW_GHASH)
            if(ASCIICHAT_SUPPORTS_CODEVIEW_GHASH)
                target_compile_options(asciichat-project-options INTERFACE -gcodeview-ghash)
            endif()
            if(BUILD_TYPE STREQUAL "Debug")
                # CodeView debug format for Debug mode
                target_compile_options(asciichat-project-options INTERFACE -gcodeview)
            elseif(BUILD_TYPE STREQUAL "Dev" OR BUILD_TYPE STREQUAL "Sanitize")
                # Dev mode: Use -g2 for full debug info and ensure PDB generation
                target_compile_options(asciichat-project-options INTERFACE -g2 -gcodeview)
                target_link_options(asciichat-project-options INTERFACE -Wl,/DEBUG:FULL)
                message(STATUS "Dev build: Enabled PDB generation with full debug info")
            endif()
        endif()
    endif()
endfunction()

# =============================================================================
# x86-64-v2/v3 Autodetection
# =============================================================================
# The detect_x86_64_level() function is now in CPUDetection.cmake
# Include it to get access to the function
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/compiler/CPUDetection.cmake)

# Configure release build optimization flags
# Args:
#   PLATFORM_DARWIN - Whether platform is macOS
#   PLATFORM_LINUX - Whether platform is Linux
#   IS_ROSETTA - Whether running under Rosetta
#   IS_APPLE_SILICON - Whether running on Apple Silicon
#   ASCIICHAT_ENABLE_CRC32_HW - Whether CRC32 hardware acceleration is enabled
#   WITH_DEBUG_INFO - Optional: If true, keep debug symbols (for RelWithDebInfo), default false
function(configure_release_flags PLATFORM_DARWIN PLATFORM_LINUX IS_ROSETTA IS_APPLE_SILICON ASCIICHAT_ENABLE_CRC32_HW)
    # Handle optional WITH_DEBUG_INFO parameter
    set(WITH_DEBUG_INFO FALSE)
    if(ARGC GREATER 5)
        set(WITH_DEBUG_INFO ${ARGV5})
    endif()

    target_compile_definitions(asciichat-project-options INTERFACE NDEBUG)

    # Disable debug info generation in release builds to prevent path embedding
    # This ensures no debug symbols or paths are embedded in release binaries
    # But keep debug info for RelWithDebInfo builds
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
        if(WITH_DEBUG_INFO)
            # RelWithDebInfo: Keep debug symbols but still optimize
            target_compile_options(asciichat-project-options INTERFACE -g)
            message(STATUS "Release build with debug info: keeping debug symbols")
        else()
            # Release: No debug info
            target_compile_options(asciichat-project-options INTERFACE -g0)
        endif()
    endif()

    configure_source_path_flags()

    # When IPO/LTO is enabled we want to retain native code in the object files
    # so developer tooling (e.g. `otool`) still recognises them as regular
    # objects. `-ffat-lto-objects` keeps the LLVM IR for link-time optimization
    # while also emitting machine code into the archive members.
    # NOTE: This is a GCC-specific flag not supported by Clang
    if(ASCIICHAT_ENABLE_IPO)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU")
            if(NOT DEFINED ASCIICHAT_SUPPORTS_FFAT_LTO_OBJECTS)
                include(CheckCCompilerFlag)
                check_c_compiler_flag("-ffat-lto-objects" ASCIICHAT_SUPPORTS_FFAT_LTO_OBJECTS)
            endif()
            if(ASCIICHAT_SUPPORTS_FFAT_LTO_OBJECTS)
                target_compile_options(asciichat-project-options INTERFACE -ffat-lto-objects)
                message(STATUS "Release build: embedding native code in LTO objects (-ffat-lto-objects)")
            else()
                message(WARNING "Release build: compiler does not support -ffat-lto-objects; static archives may contain pure LLVM bitcode")
            endif()
        elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
            # Clang doesn't support -ffat-lto-objects, but ThinLTO archives already contain object code
            message(STATUS "Release build: using Clang ThinLTO (object code preserved in archives)")
        endif()
    endif()

    # CPU-aware optimization flags
    # For RelWithDebInfo, use -O2 instead of -O3 to maintain better debug info quality
    if(WITH_DEBUG_INFO)
        target_compile_options(asciichat-project-options INTERFACE -O2 -funroll-loops -fstrict-aliasing -ftree-vectorize -pipe)
        # Keep frame pointers in RelWithDebInfo for better backtraces
        target_compile_options(asciichat-project-options INTERFACE -fno-omit-frame-pointer)
    else()
        target_compile_options(asciichat-project-options INTERFACE -O3 -funroll-loops -fstrict-aliasing -ftree-vectorize -pipe)

        if(ASCIICHAT_RELEASE_KEEP_FRAME_POINTERS)
            target_compile_options(asciichat-project-options INTERFACE -fno-omit-frame-pointer)
        else()
            target_compile_options(asciichat-project-options INTERFACE -fomit-frame-pointer)
        endif()

        # Additional safe optimizations
        target_compile_options(asciichat-project-options INTERFACE
            -foptimize-sibling-calls     # Optimize tail recursion
        )

        if(ASCIICHAT_RELEASE_ENABLE_FAST_MATH)
            target_compile_options(asciichat-project-options INTERFACE
                -fno-math-errno              # Don't set errno for math functions (faster)
                -fno-signed-zeros            # Allow optimizations that ignore sign of zero
                -freciprocal-math            # Allow reciprocal approximations
                -fassociative-math           # Allow reassociation of FP operations
                -fno-trapping-math           # Assume no floating-point exceptions
                -funsafe-math-optimizations  # Allow unsafe math optimizations (enables several flags at once)
                -ffp-contract=fast
                -ffinite-math-only
            )
        endif()

        # GCC-specific optimizations (GCC has more aggressive tree/loop opts than Clang)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU")
            target_compile_options(asciichat-project-options INTERFACE
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
                target_compile_options(asciichat-project-options INTERFACE -mllvm -polly)  # Enable Polly loop optimizer
            endif()

            # Additional Clang optimizations
            # Only use -fno-semantic-interposition on Linux
            # - macOS: Not supported (causes warning with Homebrew clang)
            # - Windows: Unused (not a shared library platform for this flag)
            if(PLATFORM_LINUX)
                target_compile_options(asciichat-project-options INTERFACE -fno-semantic-interposition)  # Allow inlining across DSO boundaries
            endif()
            # Note: -fvisibility=hidden is applied per-target in Executables.cmake (not globally)
            # This allows shared libraries to use -fvisibility=default while executables use hidden
        endif()
    endif()

    # CPU tuning selection
    set(__asciichat_cpu_flags "")
    set(__asciichat_detected_tune "")

    # Detect actual CPU architecture (CMAKE_SYSTEM_PROCESSOR can be wrong on ARM64 Windows)
    set(_actual_arch "${CMAKE_SYSTEM_PROCESSOR}")
    if(WIN32 AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(_actual_arch "ARM64")
    elseif(VCPKG_TARGET_TRIPLET MATCHES "^arm64-")
        set(_actual_arch "ARM64")
    endif()

    if(ASCIICHAT_RELEASE_CPU_TUNE STREQUAL "native")
        if(_actual_arch MATCHES "ARM64|aarch64")
            list(APPEND __asciichat_cpu_flags -mcpu=native)
        else()
            list(APPEND __asciichat_cpu_flags -march=native)
        endif()
    elseif(ASCIICHAT_RELEASE_CPU_TUNE STREQUAL "x86-64-v2")
        if(_actual_arch MATCHES "AMD64|x86_64|X86_64")
            list(APPEND __asciichat_cpu_flags -march=x86-64-v2)
        endif()
    elseif(ASCIICHAT_RELEASE_CPU_TUNE STREQUAL "x86-64-v3")
        if(_actual_arch MATCHES "AMD64|x86_64|X86_64")
            list(APPEND __asciichat_cpu_flags -march=x86-64-v3)
        endif()
    elseif(ASCIICHAT_RELEASE_CPU_TUNE STREQUAL "custom")
        if(ASCIICHAT_RELEASE_CPU_CUSTOM_FLAGS)
            separate_arguments(__asciichat_custom_flags UNIX_COMMAND "${ASCIICHAT_RELEASE_CPU_CUSTOM_FLAGS}")
            list(APPEND __asciichat_cpu_flags ${__asciichat_custom_flags})
        endif()
    else()
        # Release baseline must not depend on the build runner's extensions.
        if(_actual_arch MATCHES "AMD64|x86_64|X86_64")
            list(APPEND __asciichat_cpu_flags -march=x86-64)
            set(__asciichat_detected_tune "portable")
        elseif(_actual_arch MATCHES "ARM64|aarch64")
            set(__asciichat_baseline "armv8-a")
            list(APPEND __asciichat_cpu_flags "-march=${__asciichat_baseline}")
            set(__asciichat_detected_tune "portable")
        else()
            set(__asciichat_detected_tune "portable")
        endif()
    endif()

    if(__asciichat_cpu_flags)
        target_compile_options(asciichat-project-options INTERFACE ${__asciichat_cpu_flags})
        if(NOT ASCIICHAT_RELEASE_CPU_TUNE STREQUAL "portable")
            list(JOIN __asciichat_cpu_flags ", " __asciichat_cpu_flags_joined)
            message(STATUS "Release CPU tuning: ${BoldCyan}${ASCIICHAT_RELEASE_CPU_TUNE}${ColorReset} (${__asciichat_cpu_flags_joined})")
        elseif(__asciichat_detected_tune STREQUAL "x86-64-v3" OR __asciichat_detected_tune STREQUAL "x86-64-v2")
            list(JOIN __asciichat_cpu_flags ", " __asciichat_cpu_flags_joined)
            message(STATUS "Release CPU tuning: ${BoldCyan}portable${ColorReset} → ${BoldGreen}autodetected ${__asciichat_detected_tune}${ColorReset} (${__asciichat_cpu_flags_joined})")
        else()
            list(JOIN __asciichat_cpu_flags ", " __asciichat_cpu_flags_joined)
            message(STATUS "Release CPU tuning: ${BoldCyan}portable baseline${ColorReset} (${__asciichat_cpu_flags_joined})")
        endif()
    else()
        message(STATUS "Release CPU tuning: ${BoldCyan}${ASCIICHAT_RELEASE_CPU_TUNE}${ColorReset} (no additional CPU flags)")
    endif()

    if(APPLE)
        if(ASCIICHAT_SUPPORTS_DEAD_STRIP_DYLIBS)
            target_link_options(asciichat-project-options INTERFACE -Wl,-dead_strip_dylibs)
        endif()
    elseif(WIN32)
        if(ASCIICHAT_SUPPORTS_DYNAMICBASE)
            target_link_options(asciichat-project-options INTERFACE "LINKER:/dynamicbase")
        endif()
        if(ASCIICHAT_SUPPORTS_NXCOMPAT)
            target_link_options(asciichat-project-options INTERFACE "LINKER:/nxcompat")
        endif()
        if(ASCIICHAT_SUPPORTS_HIGHENTROPY_VA)
            target_link_options(asciichat-project-options INTERFACE "LINKER:/highentropyva")
        endif()
        if(ASCIICHAT_SUPPORTS_GUARD_CF)
            target_link_options(asciichat-project-options INTERFACE "LINKER:/guard:cf")
        endif()
        if(ASCIICHAT_SUPPORTS_BREPRO)
            target_link_options(asciichat-project-options INTERFACE "LINKER:/Brepro")
        endif()
        if(ASCIICHAT_SUPPORTS_SPECTRE_MITIGATION)
            target_compile_options(asciichat-project-options INTERFACE /Qspectre)
        endif()
    elseif(ASCIICHAT_SUPPORTS_PACK_REL_RELOCS)
        target_link_options(asciichat-project-options INTERFACE -Wl,-z,pack-relative-relocs)
    endif()

    # Link-time optimization is applied per-target in Executables.cmake
    # Not applied globally to allow shared libraries to export all symbols
    # (LTO strips "unused" symbols which are actually needed by external users)

    # =============================================================================
    # Dead Code Elimination (all platforms)
    # =============================================================================
    # Requires -ffunction-sections -fdata-sections to separate each function/data into sections
    target_compile_options(asciichat-project-options INTERFACE -ffunction-sections -fdata-sections)

    if(PLATFORM_LINUX)
        # GNU ld: Use --gc-sections to remove unused sections
        target_link_options(asciichat-project-options INTERFACE
            "$<$<NOT:$<BOOL:$<TARGET_PROPERTY:ASCIICHAT_KEEP_TEST_REGISTRATION>>>:-Wl,--gc-sections>")
    elseif(APPLE)
        # Apple ld: Use -dead_strip to remove unused sections
        target_link_options(asciichat-project-options INTERFACE
            "$<$<NOT:$<BOOL:$<TARGET_PROPERTY:ASCIICHAT_KEEP_TEST_REGISTRATION>>>:-Wl,-dead_strip>")
    elseif(WIN32)
        # MSVC linker: Use /OPT:REF and /OPT:ICF for dead code elimination and COMDAT folding
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_link_options(asciichat-project-options INTERFACE -Wl,/OPT:REF -Wl,/OPT:ICF)
        endif()
    endif()

    # =============================================================================
    # Size reduction and alignment optimizations (platform-specific)
    # =============================================================================
    if(NOT WIN32)
        if(ASCIICHAT_SUPPORTS_STACK_PROTECTOR_STRONG)
            target_compile_options(asciichat-project-options INTERFACE -fstack-protector-strong)
        endif()
        if(ASCIICHAT_SUPPORTS_STACK_CLASH_PROTECTION AND PLATFORM_LINUX)
            target_compile_options(asciichat-project-options INTERFACE -fstack-clash-protection)
        endif()
    endif()

    # All platforms: Alignment optimizations
    target_compile_options(asciichat-project-options INTERFACE -falign-loops=32 -falign-functions=32 -fmerge-all-constants)

    # Security hardening for Unix-like platforms
    if(NOT WIN32)
        if(NOT WITH_DEBUG_INFO)
            # PIE assumes executable TLS access, including after ThinLTO imports.
            # Keep library objects (including vendored libraries) PIC so they can
            # link into shared libraries. Executable linker flags are set separately.
            target_compile_options(asciichat-project-options INTERFACE "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:-fPIE>")

            if(USE_MUSL)
                # Static-PIE for musl builds (combines static linking with ASLR)
                # Note: Static-PIE uses -static-pie flag (set in Musl.cmake linker flags)
                #       and rcrt1.o startup file instead of regular -pie + crt1.o
            else()
                # RELRO and NOW flags for security (applies to all targets)
                if(PLATFORM_LINUX)
                    if(ASCIICHAT_SUPPORTS_RELRO)
                        target_link_options(asciichat-project-options INTERFACE -Wl,-z,relro)
                    endif()
                    if(ASCIICHAT_SUPPORTS_NOW)
                        target_link_options(asciichat-project-options INTERFACE -Wl,-z,now)
                    endif()
                endif()
            endif()
        endif()

        # Control Flow Integrity on supported platforms (x86_64 with CET support)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            # Intel CET (Control-flow Enforcement Technology)
            if(ASCIICHAT_SUPPORTS_CFI_FULL)
                if(CMAKE_C_COMPILER_ID MATCHES "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "8.0")
                    target_compile_options(asciichat-project-options INTERFACE -fcf-protection=full)
                elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "7.0")
                    target_compile_options(asciichat-project-options INTERFACE -fcf-protection=full)
                endif()
            endif()
        endif()

        # Linux-specific flags (not supported on macOS clang, Windows already excluded by parent if(NOT WIN32))
        if(PLATFORM_LINUX)
            # Stack clash protection (GCC 8+, Clang 11+) - not supported on macOS (causes warning)
            if(ASCIICHAT_SUPPORTS_STACK_CLASH_PROTECTION)
                target_compile_options(asciichat-project-options INTERFACE -fstack-clash-protection)
            endif()
            target_compile_options(asciichat-project-options INTERFACE -fno-plt -fno-semantic-interposition)
            target_link_options(asciichat-project-options INTERFACE -fno-plt)

            # Fortify source for additional runtime checks (not compatible with musl)
            # Undefine first to avoid redefinition warnings when makepkg/CFLAGS already set it
            if(NOT USE_MUSL)
                target_compile_options(asciichat-project-options INTERFACE "-Wp,-U_FORTIFY_SOURCE")
                target_compile_definitions(asciichat-project-options INTERFACE _FORTIFY_SOURCE=3)
            else()
                # Musl doesn't provide __*_chk fortify wrappers
                # Use -Wp, prefix to pass directly to preprocessor, overriding makepkg's -Wp,-D_FORTIFY_SOURCE=3
                target_compile_options(asciichat-project-options INTERFACE "-Wp,-U_FORTIFY_SOURCE" "-Wp,-D_FORTIFY_SOURCE=0")
            endif()
        endif()

        # macOS-specific security features
        if(PLATFORM_DARWIN)
            # Fortify source (level 2 is safer for macOS)
            target_compile_definitions(asciichat-project-options INTERFACE _FORTIFY_SOURCE=2)
        endif()
    endif()
endfunction()

# Configure coverage instrumentation (can be combined with any build type)
function(configure_coverage_flags)
    message(STATUS "Coverage instrumentation enabled")
    target_compile_definitions(asciichat-project-options INTERFACE COVERAGE_BUILD)
    target_compile_options(asciichat-project-options INTERFACE --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(asciichat-project-options INTERFACE --coverage)
endfunction()
