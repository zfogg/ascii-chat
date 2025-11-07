### Toolchain Checks Module
# Centralizes try_compile-based capability detection so the rest of the
# build logic can stay declarative.  The helper functions below rely on
# CMake's try_compile infrastructure (via the *check_* macros) to keep
# CompilerFlags.cmake free from repeated feature probes.

include(CheckCCompilerFlag)
include(CheckLinkerFlag)

set(_ASCIICHAT_CHECK_DIR "${CMAKE_BINARY_DIR}/CMakeFiles/asciichat-toolchain-checks")
file(MAKE_DIRECTORY "${_ASCIICHAT_CHECK_DIR}")

# Helper: wrap check_linker_flag with consistent caching metadata.
function(_asciichat_check_linker_flag flag result_var)
    string(MAKE_C_IDENTIFIER "ASCIICHAT_HAS_${flag}" _id_hint)
    if(NOT DEFINED ${result_var})
        check_linker_flag(C "${flag}" ${result_var})
    endif()
    set(${result_var} "${${result_var}}" CACHE BOOL "ascii-chat support for linker flag ${flag}" FORCE)
endfunction()

# Helper: wrap check_c_compiler_flag with caching.
function(_asciichat_check_compiler_flag flag result_var)
    if(NOT DEFINED ${result_var})
        check_c_compiler_flag("${flag}" ${result_var})
    endif()
    set(${result_var} "${${result_var}}" CACHE BOOL "ascii-chat support for compiler flag ${flag}" FORCE)
endfunction()

function(configure_toolchain_checks)
    # These checks all invoke try_compile underneath the hood (through
    # CMake's builtin check_* helpers).  They are separated by platform so
    # we only run probes that make sense for the active generator.

    if(NOT DEFINED ASCIICHAT_SUPPORTS_STACK_PROTECTOR_STRONG)
        _asciichat_check_compiler_flag("-fstack-protector-strong" ASCIICHAT_SUPPORTS_STACK_PROTECTOR_STRONG)
    endif()

    if(NOT DEFINED ASCIICHAT_SUPPORTS_STACK_CLASH_PROTECTION)
        _asciichat_check_compiler_flag("-fstack-clash-protection" ASCIICHAT_SUPPORTS_STACK_CLASH_PROTECTION)
    endif()

    if(WIN32)
        _asciichat_check_compiler_flag("/Qspectre" ASCIICHAT_SUPPORTS_SPECTRE_MITIGATION)
        _asciichat_check_linker_flag("LINKER:/guard:cf" ASCIICHAT_SUPPORTS_GUARD_CF)
        _asciichat_check_linker_flag("LINKER:/dynamicbase" ASCIICHAT_SUPPORTS_DYNAMICBASE)
        _asciichat_check_linker_flag("LINKER:/nxcompat" ASCIICHAT_SUPPORTS_NXCOMPAT)
        _asciichat_check_linker_flag("LINKER:/highentropyva" ASCIICHAT_SUPPORTS_HIGHENTROPY_VA)
        _asciichat_check_linker_flag("LINKER:/Brepro" ASCIICHAT_SUPPORTS_BREPRO)
    elseif(APPLE)
        _asciichat_check_linker_flag("-Wl,-dead_strip_dylibs" ASCIICHAT_SUPPORTS_DEAD_STRIP_DYLIBS)
    elseif(PLATFORM_LINUX)
        _asciichat_check_linker_flag("-Wl,-z,pack-relative-relocs" ASCIICHAT_SUPPORTS_PACK_REL_RELOCS)
        _asciichat_check_linker_flag("-Wl,-z,relro" ASCIICHAT_SUPPORTS_RELRO)
        _asciichat_check_linker_flag("-Wl,-z,now" ASCIICHAT_SUPPORTS_NOW)
    endif()

    # Generic control-flow protection flag detection (works on Clang/GCC).
    if(NOT DEFINED ASCIICHAT_SUPPORTS_CFI_FULL)
        _asciichat_check_compiler_flag("-fcf-protection=full" ASCIICHAT_SUPPORTS_CFI_FULL)
    endif()

    # Frame pointers are ubiquitous, but cache detection for completeness.
    if(NOT DEFINED ASCIICHAT_SUPPORTS_NO_OMIT_FRAME_POINTER)
        _asciichat_check_compiler_flag("-fno-omit-frame-pointer" ASCIICHAT_SUPPORTS_NO_OMIT_FRAME_POINTER)
    endif()

    mark_as_advanced(
        ASCIICHAT_SUPPORTS_STACK_PROTECTOR_STRONG
        ASCIICHAT_SUPPORTS_STACK_CLASH_PROTECTION
        ASCIICHAT_SUPPORTS_CFI_FULL
        ASCIICHAT_SUPPORTS_NO_OMIT_FRAME_POINTER
        ASCIICHAT_SUPPORTS_PACK_REL_RELOCS
        ASCIICHAT_SUPPORTS_RELRO
        ASCIICHAT_SUPPORTS_NOW
        ASCIICHAT_SUPPORTS_DEAD_STRIP_DYLIBS
        ASCIICHAT_SUPPORTS_GUARD_CF
        ASCIICHAT_SUPPORTS_DYNAMICBASE
        ASCIICHAT_SUPPORTS_NXCOMPAT
        ASCIICHAT_SUPPORTS_HIGHENTROPY_VA
        ASCIICHAT_SUPPORTS_BREPRO
        ASCIICHAT_SUPPORTS_SPECTRE_MITIGATION
    )
endfunction()


