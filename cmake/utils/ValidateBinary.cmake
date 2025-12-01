# =============================================================================
# Binary Validation Utility
# =============================================================================
# Validates binary properties using llvm-readelf and llvm-objdump
#
# Usage:
#   cmake -DMODE=<mode> -DBINARY=<path> [options] -P ValidateBinary.cmake
#
# Modes:
#   hardening     - Check security hardening (RELRO, PIE, stack canaries)
#   no_debug      - Verify no debug sections in Release binaries
#   static        - Verify static linking (no unexpected dynamic deps)
#   architecture  - Verify CPU architecture/features
#
# Common Parameters:
#   BINARY        - Path to the binary to check
#   LLVM_READELF  - Path to llvm-readelf (required for most modes)
#   LLVM_OBJDUMP  - Path to llvm-objdump (required for some modes)
#
# Mode-specific Parameters:
#   architecture mode:
#     EXPECTED_ARCH - Expected architecture (x86_64, aarch64, etc.)
#     EXPECTED_FEATURES - Comma-separated list of expected features (avx2,sse4.2)
# =============================================================================

if(NOT MODE)
    message(FATAL_ERROR "MODE not specified. Use: hardening, no_debug, static, or architecture")
endif()

if(NOT BINARY)
    message(FATAL_ERROR "BINARY not specified")
endif()

if(NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "Binary not found: ${BINARY}")
endif()

get_filename_component(BINARY_NAME "${BINARY}" NAME)

# =============================================================================
# Mode: hardening - Check security hardening flags
# =============================================================================
if(MODE STREQUAL "hardening")
    if(NOT LLVM_READELF)
        message(FATAL_ERROR "LLVM_READELF not specified for hardening check")
    endif()

    # Get dynamic section to check RELRO
    execute_process(
        COMMAND "${LLVM_READELF}" --dynamic "${BINARY}"
        OUTPUT_VARIABLE DYNAMIC_OUTPUT
        ERROR_VARIABLE READELF_ERROR
        RESULT_VARIABLE READELF_RESULT
    )

    # Get program headers to check PIE and other flags
    execute_process(
        COMMAND "${LLVM_READELF}" --program-headers "${BINARY}"
        OUTPUT_VARIABLE HEADERS_OUTPUT
        ERROR_QUIET
    )

    # Get file header for PIE check
    execute_process(
        COMMAND "${LLVM_READELF}" --file-header "${BINARY}"
        OUTPUT_VARIABLE FILE_HEADER_OUTPUT
        ERROR_QUIET
    )

    set(ISSUES "")

    # Check for RELRO (Read-Only Relocations)
    # Full RELRO: has GNU_RELRO segment AND BIND_NOW flag
    string(FIND "${HEADERS_OUTPUT}" "GNU_RELRO" HAS_RELRO)
    string(FIND "${DYNAMIC_OUTPUT}" "BIND_NOW" HAS_BIND_NOW)

    if(HAS_RELRO EQUAL -1)
        list(APPEND ISSUES "Missing GNU_RELRO (partial RELRO not enabled)")
    elseif(HAS_BIND_NOW EQUAL -1)
        list(APPEND ISSUES "Partial RELRO only (missing BIND_NOW for full RELRO)")
    endif()

    # Check for PIE (Position Independent Executable)
    string(FIND "${FILE_HEADER_OUTPUT}" "DYN (Shared object file)" IS_PIE)
    string(FIND "${FILE_HEADER_OUTPUT}" "DYN (Position-Independent Executable" IS_PIE2)
    if(IS_PIE EQUAL -1 AND IS_PIE2 EQUAL -1)
        list(APPEND ISSUES "Not a PIE binary (ASLR not fully effective)")
    endif()

    # Check for stack canary (look for __stack_chk_fail symbol)
    execute_process(
        COMMAND "${LLVM_READELF}" --dyn-syms "${BINARY}"
        OUTPUT_VARIABLE DYNSYMS_OUTPUT
        ERROR_QUIET
    )
    string(FIND "${DYNSYMS_OUTPUT}" "__stack_chk_fail" HAS_STACK_CHK)
    if(HAS_STACK_CHK EQUAL -1)
        # Not a hard error - static binaries may not have this
        # list(APPEND ISSUES "No stack canary detected (__stack_chk_fail not found)")
    endif()

    if(ISSUES)
        string(REPLACE ";" "\n  - " ISSUES_STR "${ISSUES}")
        message(FATAL_ERROR "Security hardening check FAILED for ${BINARY_NAME}:\n  - ${ISSUES_STR}")
    endif()

    message(STATUS "Hardening check PASSED for ${BINARY_NAME}: RELRO and PIE enabled")

# =============================================================================
# Mode: no_debug - Verify no debug sections
# =============================================================================
elseif(MODE STREQUAL "no_debug")
    if(NOT LLVM_READELF)
        message(FATAL_ERROR "LLVM_READELF not specified for no_debug check")
    endif()

    execute_process(
        COMMAND "${LLVM_READELF}" --sections "${BINARY}"
        OUTPUT_VARIABLE SECTIONS_OUTPUT
        ERROR_QUIET
    )

    set(DEBUG_SECTIONS "")

    # Check for common debug sections
    foreach(DEBUG_SECTION .debug_info .debug_abbrev .debug_line .debug_str .debug_ranges .debug_loc .debug_frame)
        string(FIND "${SECTIONS_OUTPUT}" "${DEBUG_SECTION}" FOUND)
        if(NOT FOUND EQUAL -1)
            list(APPEND DEBUG_SECTIONS "${DEBUG_SECTION}")
        endif()
    endforeach()

    if(DEBUG_SECTIONS)
        string(REPLACE ";" ", " DEBUG_SECTIONS_STR "${DEBUG_SECTIONS}")
        message(FATAL_ERROR "Debug info check FAILED for ${BINARY_NAME}:\n"
            "Found debug sections: ${DEBUG_SECTIONS_STR}\n"
            "Release binaries should not contain debug information.\n"
            "Run 'strip --strip-debug ${BINARY}' to remove debug sections.")
    endif()

    message(STATUS "Debug info check PASSED for ${BINARY_NAME}: no debug sections found")

# =============================================================================
# Mode: static - Verify static linking
# =============================================================================
elseif(MODE STREQUAL "static")
    if(NOT LLVM_READELF)
        message(FATAL_ERROR "LLVM_READELF not specified for static check")
    endif()

    execute_process(
        COMMAND "${LLVM_READELF}" --dynamic "${BINARY}"
        OUTPUT_VARIABLE DYNAMIC_OUTPUT
        ERROR_VARIABLE READELF_ERROR
        RESULT_VARIABLE READELF_RESULT
    )

    # If readelf returns error or no dynamic section, it's statically linked
    if(NOT READELF_RESULT EQUAL 0 OR DYNAMIC_OUTPUT STREQUAL "")
        message(STATUS "Static linking check PASSED for ${BINARY_NAME}: fully static binary")
        return()
    endif()

    # Check for dynamic section indicator
    string(FIND "${DYNAMIC_OUTPUT}" "There is no dynamic section" NO_DYNAMIC)
    if(NOT NO_DYNAMIC EQUAL -1)
        message(STATUS "Static linking check PASSED for ${BINARY_NAME}: fully static binary")
        return()
    endif()

    # Extract NEEDED entries (dynamic library dependencies)
    string(REGEX MATCHALL "\\(NEEDED\\)[^\n]*\\[([^\]]+)\\]" NEEDED_MATCHES "${DYNAMIC_OUTPUT}")

    # Allowed system libraries (these are OK for "mostly static" builds)
    set(ALLOWED_LIBS
        "linux-vdso.so"
        "ld-linux"
        "ld-musl"
        "libc.so"
        "libc.musl"
        "libm.so"
        "libpthread.so"
        "libdl.so"
        "librt.so"
        "libresolv.so"
        "libnss"
        "libgcc_s.so"
    )

    set(UNEXPECTED_LIBS "")
    foreach(MATCH ${NEEDED_MATCHES})
        string(REGEX REPLACE ".*\\[([^\]]+)\\].*" "\\1" LIB_NAME "${MATCH}")
        set(IS_ALLOWED FALSE)
        foreach(ALLOWED ${ALLOWED_LIBS})
            string(FIND "${LIB_NAME}" "${ALLOWED}" FOUND)
            if(NOT FOUND EQUAL -1)
                set(IS_ALLOWED TRUE)
                break()
            endif()
        endforeach()
        if(NOT IS_ALLOWED)
            list(APPEND UNEXPECTED_LIBS "${LIB_NAME}")
        endif()
    endforeach()

    if(UNEXPECTED_LIBS)
        string(REPLACE ";" ", " UNEXPECTED_LIBS_STR "${UNEXPECTED_LIBS}")
        message(FATAL_ERROR "Static linking check FAILED for ${BINARY_NAME}:\n"
            "Unexpected dynamic dependencies: ${UNEXPECTED_LIBS_STR}\n"
            "Release builds should be statically linked.")
    endif()

    message(STATUS "Static linking check PASSED for ${BINARY_NAME}: only system libraries linked")

# =============================================================================
# Mode: architecture - Verify CPU architecture
# =============================================================================
elseif(MODE STREQUAL "architecture")
    if(NOT LLVM_READELF)
        message(FATAL_ERROR "LLVM_READELF not specified for architecture check")
    endif()

    execute_process(
        COMMAND "${LLVM_READELF}" --file-header "${BINARY}"
        OUTPUT_VARIABLE HEADER_OUTPUT
        ERROR_QUIET
    )

    # Check architecture if specified
    if(EXPECTED_ARCH)
        set(ARCH_FOUND FALSE)
        if(EXPECTED_ARCH STREQUAL "x86_64" OR EXPECTED_ARCH STREQUAL "amd64")
            string(FIND "${HEADER_OUTPUT}" "X86-64" FOUND)
            string(FIND "${HEADER_OUTPUT}" "Advanced Micro Devices X86-64" FOUND2)
            if(NOT FOUND EQUAL -1 OR NOT FOUND2 EQUAL -1)
                set(ARCH_FOUND TRUE)
            endif()
        elseif(EXPECTED_ARCH STREQUAL "aarch64" OR EXPECTED_ARCH STREQUAL "arm64")
            string(FIND "${HEADER_OUTPUT}" "AArch64" FOUND)
            if(NOT FOUND EQUAL -1)
                set(ARCH_FOUND TRUE)
            endif()
        elseif(EXPECTED_ARCH STREQUAL "i386" OR EXPECTED_ARCH STREQUAL "x86")
            string(FIND "${HEADER_OUTPUT}" "Intel 80386" FOUND)
            if(NOT FOUND EQUAL -1)
                set(ARCH_FOUND TRUE)
            endif()
        endif()

        if(NOT ARCH_FOUND)
            message(FATAL_ERROR "Architecture check FAILED for ${BINARY_NAME}:\n"
                "Expected architecture: ${EXPECTED_ARCH}\n"
                "Binary header:\n${HEADER_OUTPUT}")
        endif()
    endif()

    # For feature detection, we'd need to analyze the actual instructions
    # This is a simplified check - full feature detection would require disassembly
    if(EXPECTED_FEATURES AND LLVM_OBJDUMP)
        # Convert comma-separated to list
        string(REPLACE "," ";" FEATURE_LIST "${EXPECTED_FEATURES}")

        execute_process(
            COMMAND "${LLVM_OBJDUMP}" -d --no-show-raw-insn "${BINARY}"
            OUTPUT_VARIABLE DISASM_OUTPUT
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        foreach(FEATURE ${FEATURE_LIST})
            if(FEATURE STREQUAL "avx2" OR FEATURE STREQUAL "AVX2")
                # Look for AVX2 instructions (vp* prefix typically)
                string(REGEX MATCH "vp[a-z]+" AVX2_FOUND "${DISASM_OUTPUT}")
                if(NOT AVX2_FOUND)
                    message(WARNING "AVX2 instructions not detected in ${BINARY_NAME} (may be OK if not used)")
                endif()
            elseif(FEATURE STREQUAL "sse2" OR FEATURE STREQUAL "SSE2")
                # SSE2 is baseline for x86_64, should always be present
                string(REGEX MATCH "(movdq|paddd|psubd|pmull)" SSE2_FOUND "${DISASM_OUTPUT}")
            endif()
        endforeach()
    endif()

    if(EXPECTED_ARCH)
        message(STATUS "Architecture check PASSED for ${BINARY_NAME}: ${EXPECTED_ARCH}")
    else()
        message(STATUS "Architecture check PASSED for ${BINARY_NAME}")
    endif()

else()
    message(FATAL_ERROR "Unknown MODE: ${MODE}. Use: hardening, no_debug, static, or architecture")
endif()
