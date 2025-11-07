# =============================================================================
# Sanitizer Configuration Module
# =============================================================================
# This module provides unified sanitizer configuration for all platforms
# and handles conflicts with mimalloc's malloc override and spinlock issues.

# Include compiler flag checking utilities
include(CheckCCompilerFlag)

# Helper function to safely add compiler flags
function(add_compiler_flag_if_supported flag)
    string(MAKE_C_IDENTIFIER "HAVE_CFLAG_${flag}" flag_var)
    check_c_compiler_flag("${flag}" ${flag_var})
    if(${flag_var})
        add_compile_options(${flag})
    endif()
endfunction()

# Configure sanitizers based on platform, compiler, and options
# Args:
#   USE_MIMALLOC - Whether mimalloc is enabled (disables all sanitizers if true)
#   BUILD_TYPE - The build type (Debug, Sanitize, TSan, etc.)
function(configure_sanitizers USE_MIMALLOC_ARG BUILD_TYPE_ARG)
    # CRITICAL: mimalloc conflicts with sanitizers in two ways:
    # 1. Malloc override: mimalloc's MI_MALLOC_OVERRIDE conflicts with ASan/LeakSan malloc interception
    # 2. Spinlock instrumentation: UBSan instruments mimalloc's spinlocks, causing infinite sched_yield() loops
    # Therefore, we disable ALL sanitizers when mimalloc is enabled

    if(USE_MIMALLOC_ARG)
        # No sanitizers with mimalloc, but preserve frame pointers for profiling
        add_compile_options(
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
        )
        message(STATUS "${BUILD_TYPE_ARG} build: Disabled all sanitizers due to mimalloc conflicts (malloc override + spinlock instrumentation)")
        return()
    endif()

    # CRITICAL: musl + sanitizers don't work together in static builds
    # Sanitizers require glibc-specific symbols like gnu_get_libc_version, dlvsym, _DYNAMIC, etc.
    # which are not available in musl. This is a known limitation.
    if(USE_MUSL)
        add_compile_options(
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
        )
        message(STATUS "${BUILD_TYPE_ARG} build: Disabled all sanitizers - incompatible with static musl builds")
        return()
    endif()

    # Sanitizer configuration based on build type
    if(BUILD_TYPE_ARG STREQUAL "Debug" OR BUILD_TYPE_ARG STREQUAL "Sanitize")
        configure_asan_ubsan_sanitizers()
    elseif(BUILD_TYPE_ARG STREQUAL "TSan")
        configure_tsan_sanitizer()
    endif()
endfunction()

# Configure AddressSanitizer + UndefinedBehaviorSanitizer + extras
function(configure_asan_ubsan_sanitizers)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Base debug flags (always supported)
        add_compile_options(-g -fno-omit-frame-pointer)

        # INFO: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html#silencing-unsigned-integer-overflow
        # Check for -fno-sanitize-merge support (not available in all Clang versions)
        add_compiler_flag_if_supported(-fno-sanitize-merge)

        # INFO: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html#disabling-instrumentation-for-common-overflow-patterns
        # Check for overflow pattern flags (newer Clang feature)
        add_compiler_flag_if_supported(-fsanitize-recover=unsigned-integer-overflow)
        add_compiler_flag_if_supported(-fsanitize-undefined-ignore-overflow-pattern=all)
        if(WIN32)
            # Windows with Clang
            add_compile_options(
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=integer
                -fsanitize=nullability
                -fsanitize=implicit-conversion
                -fsanitize=float-divide-by-zero
                -fsanitize-address-use-after-scope
                -fno-omit-frame-pointer
                -fno-optimize-sibling-calls
            )
            add_link_options(
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=integer
                -fsanitize=nullability
                -fsanitize=implicit-conversion
                -fsanitize=float-divide-by-zero
            )
            message(STATUS "Debug build: Enabled ${BoldGreen}ASan${ColorReset} + ${BoldGreen}UBSan${ColorReset} + ${BoldGreen}Integer${ColorReset} + ${BoldGreen}Nullability${ColorReset} + ${BoldGreen}ImplicitConversion${ColorReset} sanitizers")
        elseif(APPLE)
            # macOS - sanitizers except thread and leak (not supported on ARM64)
            add_compile_options(
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=integer
                -fsanitize=nullability
                -fsanitize=implicit-conversion
                -fsanitize=float-divide-by-zero
                -fsanitize-address-use-after-scope
                -fno-omit-frame-pointer
                -fno-optimize-sibling-calls
            )
            add_link_options(
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=integer
                -fsanitize=nullability
                -fsanitize=implicit-conversion
                -fsanitize=float-divide-by-zero
            )
            message(STATUS "Debug build: Enabled ${BoldGreen}ASan${ColorReset} + ${BoldGreen}UBSan${ColorReset} + ${BoldGreen}Integer${ColorReset} + ${BoldGreen}Nullability${ColorReset} + ${BoldGreen}ImplicitConversion${ColorReset} sanitizers")
        else()
            # Linux - includes LeakSanitizer
            add_compile_options(
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=leak
                -fsanitize=integer
                -fsanitize=nullability
                -fsanitize=implicit-conversion
                -fsanitize=float-divide-by-zero
                -fsanitize-address-use-after-scope
                -fno-omit-frame-pointer
                -fno-optimize-sibling-calls
            )
            add_link_options(
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=leak
                -fsanitize=integer
                -fsanitize=nullability
                -fsanitize=implicit-conversion
                -fsanitize=float-divide-by-zero
            )
            message(STATUS "Debug build: Enabled ${BoldGreen}ASan${ColorReset} + ${BoldGreen}LeakSan${ColorReset} + ${BoldGreen}UBSan${ColorReset} + ${BoldGreen}Integer${ColorReset} + ${BoldGreen}Nullability${ColorReset} + ${BoldGreen}ImplicitConversion${ColorReset} sanitizers")
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU" AND NOT WIN32)
        # GCC on Unix - disable ALL sanitizers if mimalloc is enabled (same spinlock issue as Clang)
        # Note: This is handled by the USE_MIMALLOC check at the top of configure_sanitizers()
        add_compile_options(
            -fsanitize=address
            -fsanitize=undefined
            -fsanitize=leak
            -fsanitize-address-use-after-scope
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
        )
        add_link_options(
            -fsanitize=address
            -fsanitize=undefined
            -fsanitize=leak
        )
        message(STATUS "Debug build: Enabled ${BoldGreen}ASan${ColorReset} + ${BoldGreen}LeakSan${ColorReset} + ${BoldGreen}UBSan${ColorReset} (GCC)")
    endif()
endfunction()

# Configure ThreadSanitizer
function(configure_tsan_sanitizer)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        if(WIN32)
            # ThreadSanitizer has limited support on Windows
            add_compile_options(-g -O1 -fsanitize=thread -fno-omit-frame-pointer)
            add_link_options(-fsanitize=thread)
            message(WARNING "ThreadSanitizer has limited support on Windows. Consider using Linux/macOS for full TSan support.")
        else()
            # Full TSan support on Unix-like systems
            add_compile_options(-g -O1 -fsanitize=thread -fno-omit-frame-pointer -fPIE)
            add_link_options(-fsanitize=thread LINKER:-pie)
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU" AND NOT WIN32)
        # GCC TSan support (Unix only)
        add_compile_options(-g -O1 -fsanitize=thread -fno-omit-frame-pointer -fPIE)
        add_link_options(-fsanitize=thread LINKER:-pie)
    else()
        # Fallback for compilers without TSan support
        add_compile_options(-g -O1)
        message(WARNING "ThreadSanitizer not available for this compiler/platform combination")
        message(STATUS "Consider using Clang or GCC on Linux/macOS for ThreadSanitizer support")
    endif()
endfunction()

# Copy ASAN runtime DLL on Windows (for Clang)
function(copy_asan_runtime_dll)
    if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Find the ASAN DLL in the Clang installation
        get_filename_component(CLANG_DIR ${CMAKE_C_COMPILER} DIRECTORY)
        get_filename_component(CLANG_ROOT ${CLANG_DIR} DIRECTORY)
        file(GLOB_RECURSE ASAN_DLL "${CLANG_ROOT}/lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
        if(ASAN_DLL)
            # Copy ASAN DLL to build output directory
            list(GET ASAN_DLL 0 ASAN_DLL_PATH)
            file(COPY ${ASAN_DLL_PATH} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
            message(STATUS "Copied ${BoldGreen}ASAN${ColorReset} runtime DLL: ${BoldCyan}${ASAN_DLL_PATH}${ColorReset}")
        else()
            message(WARNING "Could not find ASAN runtime DLL in Clang installation")
        endif()
    endif()
endfunction()

# Fix ASan runtime linking for Homebrew LLVM on macOS
function(fix_macos_asan_runtime)
    # Guard against multiple calls using global property
    get_property(_already_fixed GLOBAL PROPERTY _MACOS_ASAN_RUNTIME_FIXED)
    if(_already_fixed)
        return()
    endif()
    set_property(GLOBAL PROPERTY _MACOS_ASAN_RUNTIME_FIXED TRUE)

    if(APPLE AND DEFINED HOMEBREW_LLVM_LIB_DIR)
        # Find the clang version directory dynamically
        file(GLOB CLANG_VERSION_DIRS "${HOMEBREW_LLVM_LIB_DIR}/clang/*")
        if(CLANG_VERSION_DIRS)
            list(GET CLANG_VERSION_DIRS 0 CLANG_VERSION_DIR)
            get_filename_component(CLANG_VERSION_NAME "${CLANG_VERSION_DIR}" NAME)
            # Explicitly link against the correct ASan runtime from Homebrew LLVM
            add_link_options(-L${HOMEBREW_LLVM_LIB_DIR}/clang/${CLANG_VERSION_NAME}/lib/darwin)
            # NOTE: Don't add -rpath explicitly - clang automatically adds it when using -fsanitize
            message(STATUS "Using ASan runtime from: ${HOMEBREW_LLVM_LIB_DIR}/clang/${CLANG_VERSION_NAME}/lib/darwin")
        endif()
    endif()
endfunction()
