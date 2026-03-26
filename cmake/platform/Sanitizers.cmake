# =============================================================================
# Sanitizer Configuration Module
# =============================================================================
# This module provides unified sanitizer configuration for all platforms
# and handles conflicts with mimalloc's malloc override and spinlock issues.
#
# IMPORTANT: Sanitizers are now applied PER-TARGET, not globally
# This ensures dependencies are not instrumented, only project code is.
#
# Usage in CMakeLists.txt:
#   After creating a target, apply sanitizers with:
#   apply_sanitizers_to_target(my_target_name)
#
# Example:
#   add_executable(my_app main.c)
#   apply_sanitizers_to_target(my_app)
#
# Note: add_compiler_flag_if_supported() is defined in CompilerFlags.cmake
# which is included before this file

# Global list to store sanitizer flags (applied only to project targets, not dependencies)
# Use CACHE variables so they're accessible across all function scopes
set(ASCIICHAT_SANITIZER_COMPILE_FLAGS "" CACHE INTERNAL "Sanitizer compile flags")
set(ASCIICHAT_SANITIZER_LINK_FLAGS "" CACHE INTERNAL "Sanitizer link flags")
set(ASCIICHAT_ASAN_DYNAMIC_IMPORT_LIB "" CACHE INTERNAL "Windows ASan DLL import library")
set(ASCIICHAT_ASAN_RUNTIME_THUNK_LIB "" CACHE INTERNAL "Windows ASan runtime thunk library")
set(ASCIICHAT_SANITIZER_RUNTIME_DIR "" CACHE INTERNAL "Sanitizer runtime directory")

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
        # No sanitizers with mimalloc, but preserve frame pointers for profiling (applied per-target)
        set(ASCIICHAT_SANITIZER_COMPILE_FLAGS "-fno-omit-frame-pointer;-fno-optimize-sibling-calls" PARENT_SCOPE)
        message(STATUS "${BUILD_TYPE_ARG} build: Disabled all sanitizers due to mimalloc conflicts (malloc override + spinlock instrumentation)")
        return()
    endif()

    # CRITICAL: musl + sanitizers don't work together in static builds
    # Sanitizers require glibc-specific symbols like gnu_get_libc_version, dlvsym, _DYNAMIC, etc.
    # which are not available in musl. This is a known limitation.
    if(USE_MUSL)
        set(ASCIICHAT_SANITIZER_COMPILE_FLAGS "-fno-omit-frame-pointer;-fno-optimize-sibling-calls" PARENT_SCOPE)
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
        # Check if ASan runtime is available on Windows
        set(ASAN_AVAILABLE TRUE)
        if(WIN32)
            # Resolve compiler path (CMAKE_C_COMPILER can be just "clang")
            set(_clang_compiler_path "${CMAKE_C_COMPILER}")
            if(NOT IS_ABSOLUTE "${_clang_compiler_path}")
                # Use centralized ASCIICHAT_CLANG_EXECUTABLE from FindPrograms.cmake
                if(ASCIICHAT_CLANG_EXECUTABLE)
                    set(_clang_compiler_path "${ASCIICHAT_CLANG_EXECUTABLE}")
                elseif(CMAKE_CXX_COMPILER AND IS_ABSOLUTE "${CMAKE_CXX_COMPILER}")
                    # Fallback to deriving from C++ compiler directory
                    get_filename_component(_clang_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
                    if(EXISTS "${_clang_cxx_dir}/clang${CMAKE_EXECUTABLE_SUFFIX}")
                        set(_clang_compiler_path "${_clang_cxx_dir}/clang${CMAKE_EXECUTABLE_SUFFIX}")
                    endif()
                endif()
            endif()

            if(NOT IS_ABSOLUTE "${_clang_compiler_path}")
                message(WARNING "Unable to resolve absolute path for Clang compiler (${CMAKE_C_COMPILER}) - sanitizers disabled")
                return()
            endif()

            get_filename_component(CLANG_DIR "${_clang_compiler_path}" DIRECTORY)
            get_filename_component(CLANG_ROOT "${CLANG_DIR}" DIRECTORY)
            file(GLOB_RECURSE ASAN_LIB "${CLANG_ROOT}/lib/clang/*/lib/*/clang_rt.asan_dynamic*.lib")

            if(NOT ASAN_LIB)
                # Fallback to clang --print-resource-dir which is the authoritative location
                execute_process(
                    COMMAND ${_clang_compiler_path} --print-resource-dir
                    OUTPUT_VARIABLE CLANG_RESOURCE_DIR
                    RESULT_VARIABLE CLANG_RESOURCE_RESULT
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                )
                if(CLANG_RESOURCE_RESULT EQUAL 0 AND CLANG_RESOURCE_DIR)
                    file(GLOB ASAN_LIB
                        "${CLANG_RESOURCE_DIR}/lib/windows*/clang_rt.asan_dynamic*.lib"
                        "${CLANG_RESOURCE_DIR}/lib/windows*/clang_rt.asan_dbg_dynamic*.lib"
                        "${CLANG_RESOURCE_DIR}/lib/windows*/clang_rt.asan_cxx_dynamic*.lib"
                    )
                endif()
            endif()

            if(NOT ASAN_LIB)
                set(ASAN_AVAILABLE FALSE)
                message(WARNING "ASan runtime libraries not found in LLVM installation - sanitizers disabled")
                message(STATUS "  Searched: ${CLANG_ROOT}/lib/clang/*/lib/*/clang_rt.asan_dynamic*.lib")
                if(CLANG_RESOURCE_DIR)
                    message(STATUS "  Also checked resource dir: ${CLANG_RESOURCE_DIR}/lib/windows*")
                endif()
                message(STATUS "  To enable sanitizers, install LLVM with sanitizer runtimes or use Dev build")
                return()
            endif()

            # Extract specific ASan libraries for early linking
            # The ASan DLL must load BEFORE other DLLs (like MSVCP140D.dll) to track their allocations
            # This is achieved by making it first in the Import Directory Table via link order
            # See: https://github.com/llvm/llvm-project/issues/61685
            set(ASAN_DYNAMIC_IMPORT_LIB "")
            set(ASAN_RUNTIME_THUNK_LIB "")
            foreach(_lib ${ASAN_LIB})
                # Get just the filename
                get_filename_component(_lib_name "${_lib}" NAME)
                # Match the main import library (not dbg or cxx variants)
                if(_lib_name MATCHES "^clang_rt\\.asan_dynamic-x86_64\\.lib$")
                    set(ASAN_DYNAMIC_IMPORT_LIB "${_lib}")
                endif()
            endforeach()

            # Find the runtime thunk library for wholearchive linking
            get_filename_component(_asan_lib_dir "${ASAN_DYNAMIC_IMPORT_LIB}" DIRECTORY)
            if(EXISTS "${_asan_lib_dir}/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
                set(ASAN_RUNTIME_THUNK_LIB "${_asan_lib_dir}/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
            endif()

            if(NOT ASAN_DYNAMIC_IMPORT_LIB)
                message(WARNING "Could not find clang_rt.asan_dynamic-x86_64.lib - ASan DLL load order may cause false positives")
            endif()
        endif()

        # Base debug flags (always supported) - append to global sanitizer flags
        list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS -g -fno-omit-frame-pointer)

        # INFO: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html#silencing-unsigned-integer-overflow
        # Check for -fno-sanitize-merge support (not available in all Clang versions)
        # These will be conditionally added to sanitizer flags if supported
        include(CheckCCompilerFlag)
        check_c_compiler_flag(-fno-sanitize-merge HAS_FNOASAN_MERGE)
        if(HAS_FNOASAN_MERGE)
            list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS -fno-sanitize-merge)
        endif()

        # INFO: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html#disabling-instrumentation-for-common-overflow-patterns
        # Check for overflow pattern flags (newer Clang feature)
        check_c_compiler_flag(-fsanitize-recover=unsigned-integer-overflow HAS_SANITIZE_RECOVER)
        if(HAS_SANITIZE_RECOVER)
            list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS -fsanitize-recover=unsigned-integer-overflow)
        endif()

        check_c_compiler_flag(-fsanitize-undefined-ignore-overflow-pattern=all HAS_SANITIZE_OVERFLOW_PATTERN)
        if(HAS_SANITIZE_OVERFLOW_PATTERN)
            list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS -fsanitize-undefined-ignore-overflow-pattern=all)
        endif()
        if(WIN32)
            # Windows with Clang - use RELEASE CRT (/MD) with ASan
            # ASan does NOT support the debug CRT (/MDd) because debug CRT's memory tracking
            # interferes with ASan's malloc/free interposition. This is documented behavior:
            # https://learn.microsoft.com/en-us/cpp/sanitizers/asan-runtime
            # "ASan does not support linking with the debug CRT versions"
            #
            # This is NOT a limitation - ASan provides SUPERIOR memory debugging compared
            # to the debug CRT's checks. You get:
            # - Better memory error detection (use-after-free, buffer overflows, etc.)
            # - Full debug symbols (-g) preserved
            # - Stack traces for all memory errors
            list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=integer
                -fsanitize=nullability
                -fsanitize=implicit-conversion
                -fsanitize=float-divide-by-zero
                -fsanitize-address-use-after-scope
                -fno-omit-frame-pointer
                -fno-optimize-sibling-calls
                -D_MT
                -D_DLL
            )

            # Store Windows-specific ASan linking info in global variables (will be applied per-target)
            set(ASCIICHAT_ASAN_DYNAMIC_IMPORT_LIB "${ASAN_DYNAMIC_IMPORT_LIB}" PARENT_SCOPE)
            set(ASCIICHAT_ASAN_RUNTIME_THUNK_LIB "${ASAN_RUNTIME_THUNK_LIB}" PARENT_SCOPE)

            if(ASAN_DYNAMIC_IMPORT_LIB)
                message(STATUS "  ASan import library (will be linked first on per-target basis): ${ASAN_DYNAMIC_IMPORT_LIB}")
            endif()

            if(ASAN_RUNTIME_THUNK_LIB)
                message(STATUS "  ASan runtime thunk (will be linked with wholearchive on per-target basis): ${ASAN_RUNTIME_THUNK_LIB}")
            endif()

            # Link options for target-specific application
            list(APPEND ASCIICHAT_SANITIZER_LINK_FLAGS
                -fsanitize=undefined
                -fsanitize=integer
                -fsanitize=nullability
                -fsanitize=implicit-conversion
                -fsanitize=float-divide-by-zero
            )
            message(STATUS "Debug build: Sanitizers configured for ${BoldGreen}ASan${ColorReset} + ${BoldGreen}UBSan${ColorReset} + ${BoldGreen}Integer${ColorReset} + ${BoldGreen}Nullability${ColorReset} + ${BoldGreen}ImplicitConversion${ColorReset} (will apply to project targets only)")
        elseif(APPLE)
            # macOS - sanitizers except thread and leak (not supported on ARM64)
            list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS
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
            list(APPEND ASCIICHAT_SANITIZER_LINK_FLAGS
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=integer
                -fsanitize=nullability
                -fsanitize=implicit-conversion
                -fsanitize=float-divide-by-zero
            )
            message(STATUS "Debug build: Sanitizers configured for ${BoldGreen}ASan${ColorReset} + ${BoldGreen}UBSan${ColorReset} + ${BoldGreen}Integer${ColorReset} + ${BoldGreen}Nullability${ColorReset} + ${BoldGreen}ImplicitConversion${ColorReset} (will apply to project targets only)")
        else()
            # Linux - includes LeakSanitizer
            # Use -shared-libsan to link sanitizer runtimes as shared libraries
            # This is required when the main binary loads shared libraries that are also sanitized
            list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS
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
                -shared-libsan
            )
            # Find the clang runtime library directory for rpath
            # First try --print-runtime-dir (may return non-existent path on some systems)
            execute_process(
                COMMAND ${CMAKE_C_COMPILER} --print-runtime-dir
                OUTPUT_VARIABLE CLANG_RUNTIME_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            # If --print-runtime-dir fails, returns non-existent path, or the directory
            # doesn't contain the ASan shared library, try resource-dir/lib/linux instead.
            # Some LLVM installations (e.g. Homebrew) put the .so in lib/linux/ while
            # --print-runtime-dir points to lib/x86_64-unknown-linux-gnu/ (static libs only).
            file(GLOB _asan_so "${CLANG_RUNTIME_DIR}/libclang_rt.asan-*.so")
            if(NOT CLANG_RUNTIME_DIR OR NOT EXISTS "${CLANG_RUNTIME_DIR}" OR NOT _asan_so)
                execute_process(
                    COMMAND ${CMAKE_C_COMPILER} --print-resource-dir
                    OUTPUT_VARIABLE CLANG_RESOURCE_DIR
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                )
                if(CLANG_RESOURCE_DIR AND EXISTS "${CLANG_RESOURCE_DIR}/lib/linux")
                    set(CLANG_RUNTIME_DIR "${CLANG_RESOURCE_DIR}/lib/linux")
                endif()
            endif()
            if(CLANG_RUNTIME_DIR AND EXISTS "${CLANG_RUNTIME_DIR}")
                list(APPEND ASCIICHAT_SANITIZER_LINK_FLAGS
                    -fsanitize=address
                    -fsanitize=undefined
                    -fsanitize=leak
                    -fsanitize=integer
                    -fsanitize=nullability
                    -fsanitize=implicit-conversion
                    -fsanitize=float-divide-by-zero
                    -shared-libsan
                    "LINKER:-rpath,${CLANG_RUNTIME_DIR}"
                )
                # Store runtime dir for per-target application
                set(ASCIICHAT_SANITIZER_RUNTIME_DIR "${CLANG_RUNTIME_DIR}" PARENT_SCOPE)
                message(STATUS "Debug build: Sanitizers configured for ${BoldGreen}ASan${ColorReset} + ${BoldGreen}LeakSan${ColorReset} + ${BoldGreen}UBSan${ColorReset} + ${BoldGreen}Integer${ColorReset} + ${BoldGreen}Nullability${ColorReset} + ${BoldGreen}ImplicitConversion${ColorReset} (will apply to project targets only)")
                message(STATUS "  Sanitizer runtime: ${CLANG_RUNTIME_DIR}")
            else()
                message(FATAL_ERROR "Could not find Clang sanitizer runtime directory.\n"
                    "Tried:\n"
                    "  - clang --print-runtime-dir\n"
                    "  - clang --print-resource-dir + /lib/linux\n"
                    "Install libclang-rt-*-dev package or use cmake -DCMAKE_BUILD_TYPE=Dev to disable sanitizers.")
            endif()
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU" AND NOT WIN32)
        # GCC on Unix - disable ALL sanitizers if mimalloc is enabled (same spinlock issue as Clang)
        # Note: This is handled by the USE_MIMALLOC check at the top of configure_sanitizers()
        list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS
            -fsanitize=address
            -fsanitize=undefined
            -fsanitize=leak
            -fsanitize-address-use-after-scope
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
        )
        list(APPEND ASCIICHAT_SANITIZER_LINK_FLAGS
            -fsanitize=address
            -fsanitize=undefined
            -fsanitize=leak
        )
        message(STATUS "Debug build: Sanitizers configured for ${BoldGreen}ASan${ColorReset} + ${BoldGreen}LeakSan${ColorReset} + ${BoldGreen}UBSan${ColorReset} (GCC, will apply to project targets only)")
    endif()

    # Update CACHE variables so they're accessible globally
    set(ASCIICHAT_SANITIZER_COMPILE_FLAGS "${ASCIICHAT_SANITIZER_COMPILE_FLAGS}" CACHE INTERNAL "Sanitizer compile flags" FORCE)
    set(ASCIICHAT_SANITIZER_LINK_FLAGS "${ASCIICHAT_SANITIZER_LINK_FLAGS}" CACHE INTERNAL "Sanitizer link flags" FORCE)
    set(ASCIICHAT_ASAN_DYNAMIC_IMPORT_LIB "${ASCIICHAT_ASAN_DYNAMIC_IMPORT_LIB}" CACHE INTERNAL "Windows ASan DLL import library" FORCE)
    set(ASCIICHAT_ASAN_RUNTIME_THUNK_LIB "${ASCIICHAT_ASAN_RUNTIME_THUNK_LIB}" CACHE INTERNAL "Windows ASan runtime thunk library" FORCE)
    set(ASCIICHAT_SANITIZER_RUNTIME_DIR "${ASCIICHAT_SANITIZER_RUNTIME_DIR}" CACHE INTERNAL "Sanitizer runtime directory" FORCE)
endfunction()

# Configure ThreadSanitizer
function(configure_tsan_sanitizer)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        if(WIN32)
            # ThreadSanitizer has limited support on Windows
            list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS -g -O1 -fsanitize=thread -fno-omit-frame-pointer)
            list(APPEND ASCIICHAT_SANITIZER_LINK_FLAGS -fsanitize=thread)
            message(WARNING "ThreadSanitizer has limited support on Windows. Consider using Linux/macOS for full TSan support.")
        else()
            # Full TSan support on Unix-like systems
            list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS -g -O1 -fsanitize=thread -fno-omit-frame-pointer -fPIE)
            list(APPEND ASCIICHAT_SANITIZER_LINK_FLAGS -fsanitize=thread LINKER:-pie)
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU" AND NOT WIN32)
        # GCC TSan support (Unix only)
        list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS -g -O1 -fsanitize=thread -fno-omit-frame-pointer -fPIE)
        list(APPEND ASCIICHAT_SANITIZER_LINK_FLAGS -fsanitize=thread LINKER:-pie)
    else()
        # Fallback for compilers without TSan support
        list(APPEND ASCIICHAT_SANITIZER_COMPILE_FLAGS -g -O1)
        message(WARNING "ThreadSanitizer not available for this compiler/platform combination")
        message(STATUS "Consider using Clang or GCC on Linux/macOS for ThreadSanitizer support")
    endif()

    # Update CACHE variables so they're accessible globally
    set(ASCIICHAT_SANITIZER_COMPILE_FLAGS "${ASCIICHAT_SANITIZER_COMPILE_FLAGS}" CACHE INTERNAL "Sanitizer compile flags" FORCE)
    set(ASCIICHAT_SANITIZER_LINK_FLAGS "${ASCIICHAT_SANITIZER_LINK_FLAGS}" CACHE INTERNAL "Sanitizer link flags" FORCE)
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

    if(APPLE AND DEFINED LLVM_ROOT_PREFIX)
        # Derive the LLVM lib directory from LLVM_ROOT_PREFIX (single source of truth)
        set(_llvm_lib_dir "${LLVM_ROOT_PREFIX}/lib")

        # Find the clang version directory dynamically
        file(GLOB CLANG_VERSION_DIRS "${_llvm_lib_dir}/clang/*")
        if(CLANG_VERSION_DIRS)
            list(GET CLANG_VERSION_DIRS 0 CLANG_VERSION_DIR)
            get_filename_component(CLANG_VERSION_NAME "${CLANG_VERSION_DIR}" NAME)
            # Explicitly link against the correct ASan runtime from LLVM
            add_link_options(-L${_llvm_lib_dir}/clang/${CLANG_VERSION_NAME}/lib/darwin)
            # NOTE: Don't add -rpath explicitly - clang automatically adds it when using -fsanitize
            message(STATUS "Using ASan runtime from: ${_llvm_lib_dir}/clang/${CLANG_VERSION_NAME}/lib/darwin")
        endif()
        unset(_llvm_lib_dir)
    endif()
endfunction()

# Apply configured sanitizers to a specific target
# This function applies sanitizers ONLY to project code, not to dependencies
# Usage: apply_sanitizers_to_target(my_target_name)
function(apply_sanitizers_to_target TARGET_NAME)
    if(NOT ASCIICHAT_SANITIZER_COMPILE_FLAGS)
        # No sanitizers configured, nothing to do
        return()
    endif()

    # Apply compile options
    target_compile_options(${TARGET_NAME} PRIVATE ${ASCIICHAT_SANITIZER_COMPILE_FLAGS})

    # Apply link options
    if(ASCIICHAT_SANITIZER_LINK_FLAGS)
        target_link_options(${TARGET_NAME} PRIVATE ${ASCIICHAT_SANITIZER_LINK_FLAGS})
    endif()

    # Windows-specific: Link ASan import library FIRST (must be earliest in import table)
    if(WIN32 AND ASCIICHAT_ASAN_DYNAMIC_IMPORT_LIB)
        # Use LINK_LIBRARIES instead of target_link_libraries to ensure it comes first
        # But since this is a per-target function, we need to use target_link_libraries
        # and hope the linker respects the order (it usually does with modern linkers)
        target_link_libraries(${TARGET_NAME} PRIVATE "${ASCIICHAT_ASAN_DYNAMIC_IMPORT_LIB}")

        # Link the runtime thunk with /wholearchive
        if(ASCIICHAT_ASAN_RUNTIME_THUNK_LIB)
            target_link_options(${TARGET_NAME} PRIVATE "-Xlinker" "/wholearchive:${ASCIICHAT_ASAN_RUNTIME_THUNK_LIB}")
        endif()
    endif()
endfunction()
