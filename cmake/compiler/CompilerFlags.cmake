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

# Include CMake modules for compiler/linker flag checking
include(CheckCCompilerFlag)
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
        check_linker_flag(C "-Wl,-z,pack-relative-relocs" ASCIICHAT_SUPPORTS_PACK_REL_RELOCS)
        check_linker_flag(C "-Wl,-z,relro" ASCIICHAT_SUPPORTS_RELRO)
        check_linker_flag(C "-Wl,-z,now" ASCIICHAT_SUPPORTS_NOW)
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
        add_compile_options(${flag})
    endif()
endfunction()

# Check and add linker flag if supported
function(add_linker_flag_if_supported flag)
    string(MAKE_C_IDENTIFIER "HAVE_LFLAG_${flag}" flag_var)
    check_linker_flag(C "${flag}" ${flag_var})
    if(${flag_var})
        add_link_options(${flag})
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
    if(ASCIICHAT_SUPPORTS_NO_OMIT_FRAME_POINTER AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
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
        # Check if lld is available before trying to use it
        # Search in Homebrew LLVM paths first (not in default PATH on macOS)
        find_program(LLD_LINKER NAMES ld.lld lld
            HINTS
                /opt/homebrew/opt/llvm/bin    # Homebrew on Apple Silicon
                /usr/local/opt/llvm/bin        # Homebrew on Intel Mac
        )
        if(LLD_LINKER)
            # Verify the linker actually works (handles broken symlinks)
            execute_process(
                COMMAND "${LLD_LINKER}" --version
                RESULT_VARIABLE LLD_CHECK_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(LLD_CHECK_RESULT EQUAL 0)
                add_link_options(-fuse-ld=lld)
                message(STATUS "Using ${BoldCyan}LLD linker${ColorReset} for faster Debug/Dev builds")
            else()
                message(STATUS "${Yellow}LLD linker found but not functional${ColorReset} - using default linker")
                unset(LLD_LINKER CACHE)
            endif()
        else()
            message(STATUS "${Yellow}LLD linker not found${ColorReset} - using default linker")
        endif()
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

# =============================================================================
# x86-64-v2/v3 Autodetection
# =============================================================================
# Detects the highest supported x86-64 microarchitecture level on the build machine
# Returns: "x86-64-v3", "x86-64-v2", or "portable" in DETECTED_LEVEL
# =============================================================================
function(detect_x86_64_level DETECTED_LEVEL)
    set(${DETECTED_LEVEL} "portable" PARENT_SCOPE)

    # Only detect for x86_64 builds
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|X86_64")
        return()
    endif()

    # Skip detection if cross-compiling (can't run test programs)
    if(CMAKE_CROSSCOMPILING)
        return()
    endif()

    # Check if compiler supports x86-64-v3 flag (GCC 11+, Clang 12+)
    set(CMAKE_REQUIRED_FLAGS_SAVE ${CMAKE_REQUIRED_FLAGS})
    check_c_compiler_flag("-march=x86-64-v3" COMPILER_SUPPORTS_V3_FLAG)
    if(NOT COMPILER_SUPPORTS_V3_FLAG)
        set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
        return()
    endif()

    set(CMAKE_REQUIRED_FLAGS "-march=x86-64-v3")

    # Test if compiler accepts x86-64-v3 and CPU supports it
    # x86-64-v3 requires: AVX, AVX2, BMI1, BMI2, F16C, FMA, LZCNT, MOVBE, OSXSAVE
    check_c_source_runs("
        #include <stdint.h>

        #ifdef _WIN32
        #include <intrin.h>
        #else
        #include <cpuid.h>
        #endif

        int main() {
            #ifdef _WIN32
            int info[4];
            // Check AVX (CPUID leaf 1, ECX bit 28)
            __cpuid(info, 1);
            if (!(info[2] & (1 << 28))) return 1;
            // Check AVX2 (CPUID leaf 7, EBX bit 5)
            __cpuidex(info, 7, 0);
            if (!(info[1] & (1 << 5))) return 1;
            // Check BMI1 (CPUID leaf 7, EBX bit 3)
            if (!(info[1] & (1 << 3))) return 1;
            // Check BMI2 (CPUID leaf 7, EBX bit 8)
            if (!(info[1] & (1 << 8))) return 1;
            // Check FMA (CPUID leaf 1, ECX bit 12)
            __cpuid(info, 1);
            if (!(info[2] & (1 << 12))) return 1;
            // Check F16C (CPUID leaf 1, ECX bit 29)
            if (!(info[2] & (1 << 29))) return 1;
            // Check LZCNT (CPUID leaf 0x80000001, ECX bit 5)
            __cpuidex(info, 0x80000001, 0);
            if (!(info[2] & (1 << 5))) return 1;
            // Check MOVBE (CPUID leaf 1, ECX bit 22)
            __cpuid(info, 1);
            if (!(info[2] & (1 << 22))) return 1;
            // Check OSXSAVE (CPUID leaf 1, ECX bit 27)
            if (!(info[2] & (1 << 27))) return 1;
            #else
            unsigned int eax, ebx, ecx, edx;
            // Check AVX (CPUID leaf 1, ECX bit 28)
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_AVX)) return 1;
            // Check AVX2 (CPUID leaf 7, subleaf 0, EBX bit 5)
            if (!__get_cpuid_max(0, NULL)) return 1;
            if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ebx & bit_AVX2)) return 1;
            // Check BMI1 (CPUID leaf 7, EBX bit 3)
            if (!(ebx & bit_BMI)) return 1;
            // Check BMI2 (CPUID leaf 7, EBX bit 8)
            if (!(ebx & bit_BMI2)) return 1;
            // Check FMA (CPUID leaf 1, ECX bit 12)
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_FMA)) return 1;
            // Check F16C (CPUID leaf 1, ECX bit 29)
            if (!(ecx & bit_F16C)) return 1;
            // Check LZCNT (CPUID leaf 0x80000001, ECX bit 5)
            if (!__get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_LZCNT)) return 1;
            // Check MOVBE (CPUID leaf 1, ECX bit 22)
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_MOVBE)) return 1;
            // Check OSXSAVE (CPUID leaf 1, ECX bit 27)
            if (!(ecx & bit_OSXSAVE)) return 1;
            #endif
            return 0;
        }
    " CPU_SUPPORTS_V3)

    if(CPU_SUPPORTS_V3)
        set(${DETECTED_LEVEL} "x86-64-v3" PARENT_SCOPE)
        set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
        return()
    endif()

    # Check if compiler supports x86-64-v2 flag (GCC 11+, Clang 12+)
    check_c_compiler_flag("-march=x86-64-v2" COMPILER_SUPPORTS_V2_FLAG)
    if(NOT COMPILER_SUPPORTS_V2_FLAG)
        set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
        return()
    endif()

    set(CMAKE_REQUIRED_FLAGS "-march=x86-64-v2")

    # Test if compiler accepts x86-64-v2 and CPU supports it
    # x86-64-v2 requires: CMPXCHG16B, LAHF-SAHF, POPCNT, SSE3, SSE4.1, SSE4.2, SSSE3
    check_c_source_runs("
        #include <stdint.h>

        #ifdef _WIN32
        #include <intrin.h>
        #else
        #include <cpuid.h>
        #endif

        int main() {
            #ifdef _WIN32
            int info[4];
            // Check CMPXCHG16B (CPUID leaf 1, ECX bit 13)
            __cpuid(info, 1);
            if (!(info[2] & (1 << 13))) return 1;
            // Check LAHF-SAHF (CPUID leaf 0x80000001, ECX bit 0)
            __cpuidex(info, 0x80000001, 0);
            if (!(info[2] & (1 << 0))) return 1;
            // Check POPCNT (CPUID leaf 1, ECX bit 23)
            __cpuid(info, 1);
            if (!(info[2] & (1 << 23))) return 1;
            // Check SSE3 (CPUID leaf 1, ECX bit 0)
            if (!(info[2] & (1 << 0))) return 1;
            // Check SSE4.1 (CPUID leaf 1, ECX bit 19)
            if (!(info[2] & (1 << 19))) return 1;
            // Check SSE4.2 (CPUID leaf 1, ECX bit 20)
            if (!(info[2] & (1 << 20))) return 1;
            // Check SSSE3 (CPUID leaf 1, ECX bit 9)
            if (!(info[2] & (1 << 9))) return 1;
            #else
            unsigned int eax, ebx, ecx, edx;
            // Check CMPXCHG16B (CPUID leaf 1, ECX bit 13)
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_CMPXCHG16B)) return 1;
            // Check LAHF-SAHF (CPUID leaf 0x80000001, ECX bit 0)
            if (!__get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_LAHF)) return 1;
            // Check POPCNT (CPUID leaf 1, ECX bit 23)
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 1;
            if (!(ecx & bit_POPCNT)) return 1;
            // Check SSE3 (CPUID leaf 1, ECX bit 0)
            if (!(ecx & bit_SSE3)) return 1;
            // Check SSE4.1 (CPUID leaf 1, ECX bit 19)
            if (!(ecx & bit_SSE4_1)) return 1;
            // Check SSE4.2 (CPUID leaf 1, ECX bit 20)
            if (!(ecx & bit_SSE4_2)) return 1;
            // Check SSSE3 (CPUID leaf 1, ECX bit 9)
            if (!(ecx & bit_SSSE3)) return 1;
            #endif
            return 0;
        }
    " CPU_SUPPORTS_V2)

    set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})

    if(CPU_SUPPORTS_V2)
        set(${DETECTED_LEVEL} "x86-64-v2" PARENT_SCOPE)
    endif()
endfunction()

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
                add_compile_options(-ffat-lto-objects)
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
        add_compile_options(-O2 -funroll-loops -fstrict-aliasing -ftree-vectorize -pipe)
        # Keep frame pointers in RelWithDebInfo for better backtraces
        add_compile_options(-fno-omit-frame-pointer)
    else()
        add_compile_options(-O3 -funroll-loops -fstrict-aliasing -ftree-vectorize -pipe)

        if(ASCIICHAT_RELEASE_KEEP_FRAME_POINTERS)
            add_compile_options(-fno-omit-frame-pointer)
        else()
            add_compile_options(-fomit-frame-pointer)
        endif()

        # Additional safe optimizations
        add_compile_options(
            -foptimize-sibling-calls     # Optimize tail recursion
        )

        if(ASCIICHAT_RELEASE_ENABLE_FAST_MATH)
            add_compile_options(
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

    # CPU tuning selection
    set(__asciichat_cpu_flags "")
    set(__asciichat_detected_tune "")

    if(ASCIICHAT_RELEASE_CPU_TUNE STREQUAL "native")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            list(APPEND __asciichat_cpu_flags -mcpu=native)
        else()
            list(APPEND __asciichat_cpu_flags -march=native)
        endif()
    elseif(ASCIICHAT_RELEASE_CPU_TUNE STREQUAL "x86-64-v2")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|X86_64")
            list(APPEND __asciichat_cpu_flags -march=x86-64-v2)
        endif()
    elseif(ASCIICHAT_RELEASE_CPU_TUNE STREQUAL "x86-64-v3")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|X86_64")
            list(APPEND __asciichat_cpu_flags -march=x86-64-v3)
        endif()
    elseif(ASCIICHAT_RELEASE_CPU_TUNE STREQUAL "custom")
        if(ASCIICHAT_RELEASE_CPU_CUSTOM_FLAGS)
            separate_arguments(__asciichat_custom_flags UNIX_COMMAND "${ASCIICHAT_RELEASE_CPU_CUSTOM_FLAGS}")
            list(APPEND __asciichat_cpu_flags ${__asciichat_custom_flags})
        endif()
    else()
        # portable baseline (default) - autodetect x86-64-v2/v3 if supported
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|X86_64")
            # Autodetect the highest supported x86-64 microarchitecture level
            detect_x86_64_level(__asciichat_detected_tune)

            if(__asciichat_detected_tune STREQUAL "x86-64-v3")
                list(APPEND __asciichat_cpu_flags -march=x86-64-v3)
                set(__asciichat_detected_tune "x86-64-v3")
            elseif(__asciichat_detected_tune STREQUAL "x86-64-v2")
                list(APPEND __asciichat_cpu_flags -march=x86-64-v2)
                set(__asciichat_detected_tune "x86-64-v2")
            else()
                # Fall back to portable baseline
                list(APPEND __asciichat_cpu_flags -march=x86-64)
                set(__asciichat_detected_tune "portable")
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(__asciichat_baseline "armv8-a")
            if(ASCIICHAT_ENABLE_CRC32_HW)
                set(__asciichat_baseline "${__asciichat_baseline}+crc")
            endif()
            list(APPEND __asciichat_cpu_flags "-march=${__asciichat_baseline}")
            set(__asciichat_detected_tune "portable")
        else()
            set(__asciichat_detected_tune "portable")
        endif()
    endif()

    if(__asciichat_cpu_flags)
        add_compile_options(${__asciichat_cpu_flags})
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
            add_link_options(-Wl,-dead_strip_dylibs)
        endif()
    elseif(WIN32)
        if(ASCIICHAT_SUPPORTS_DYNAMICBASE)
            add_link_options("LINKER:/dynamicbase")
        endif()
        if(ASCIICHAT_SUPPORTS_NXCOMPAT)
            add_link_options("LINKER:/nxcompat")
        endif()
        if(ASCIICHAT_SUPPORTS_HIGHENTROPY_VA)
            add_link_options("LINKER:/highentropyva")
        endif()
        if(ASCIICHAT_SUPPORTS_GUARD_CF)
            add_link_options("LINKER:/guard:cf")
        endif()
        if(ASCIICHAT_SUPPORTS_BREPRO)
            add_link_options("LINKER:/Brepro")
        endif()
        if(ASCIICHAT_SUPPORTS_SPECTRE_MITIGATION)
            add_compile_options(/Qspectre)
        endif()
    elseif(ASCIICHAT_SUPPORTS_PACK_REL_RELOCS)
        add_link_options(-Wl,-z,pack-relative-relocs)
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
        if(ASCIICHAT_SUPPORTS_STACK_PROTECTOR_STRONG)
            add_compile_options(-fstack-protector-strong)
        endif()
        if(ASCIICHAT_SUPPORTS_STACK_CLASH_PROTECTION AND PLATFORM_LINUX)
            add_compile_options(-fstack-clash-protection)
        endif()
    endif()

    # All platforms: Alignment optimizations
    add_compile_options(-falign-loops=32 -falign-functions=32 -fmerge-all-constants)

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
                    if(ASCIICHAT_SUPPORTS_RELRO)
                        add_link_options(-Wl,-z,relro)
                    endif()
                    if(ASCIICHAT_SUPPORTS_NOW)
                        add_link_options(-Wl,-z,now)
                    endif()
                elseif(APPLE)
                    add_link_options(-Wl,-pie)
                endif()
            endif()
        endif()

        # Control Flow Integrity on supported platforms (x86_64 with CET support)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            # Intel CET (Control-flow Enforcement Technology)
            if(ASCIICHAT_SUPPORTS_CFI_FULL)
                if(CMAKE_C_COMPILER_ID MATCHES "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "8.0")
                    add_compile_options(-fcf-protection=full)
                elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "7.0")
                    add_compile_options(-fcf-protection=full)
                endif()
            endif()
        endif()

        # Linux-specific flags (not supported on macOS clang, Windows already excluded by parent if(NOT WIN32))
        if(PLATFORM_LINUX)
            # Stack clash protection (GCC 8+, Clang 11+) - not supported on macOS (causes warning)
            if(ASCIICHAT_SUPPORTS_STACK_CLASH_PROTECTION)
                add_compile_options(-fstack-clash-protection)
            endif()
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
