include_guard(GLOBAL)

# Used by the toolchain and source dependencies after runtime/header staging.
function(asciichat_musl_compile_flags c_flags cxx_flags)
    set(_c "-fno-lto -ftls-model=global-dynamic -fPIC -nostdlibinc -isystem ${MUSL_INCLUDE_DIR}")
    if(KERNEL_HEADERS_DIR)
        string(APPEND _c " -isystem ${KERNEL_HEADERS_DIR}")
    endif()
    # libc++ wraps C headers with include_next; target C headers must follow it,
    # including during the first toolchain pass before libc++ has been staged.
    string(REPLACE "-isystem" "-idirafter" _cxx "${_c}")
    # Some third-party C++ targets inherit the musl flags while selecting a
    # non-libc++ standard library; Clang then reports -stdlib as unused.
    # Keep libc++ selected for the project, but avoid noisy diagnostics from
    # those dependency compile commands.
    string(APPEND _cxx " -stdlib=libc++ -nostdinc++ -Wno-unused-command-line-argument")
    # Clang diagnoses precedence and duplicate-platform-macro patterns in the
    # external musl headers; keep these diagnostics out of project output.
    string(APPEND _c " -Wno-bitwise-op-parentheses -Wno-shift-op-parentheses -Wno-macro-redefined")
    string(APPEND _cxx " -Wno-bitwise-op-parentheses -Wno-shift-op-parentheses -Wno-macro-redefined")
    if(ALPINE_LIBCXX_INCLUDE_DIR)
        string(APPEND _cxx " -isystem ${ALPINE_LIBCXX_INCLUDE_DIR}")
    endif()
    if(MUSL_TARGET MATCHES "^aarch64")
        string(APPEND _c " -mno-outline-atomics")
        string(APPEND _cxx " -mno-outline-atomics")
    endif()
    set(${c_flags} "${_c}" PARENT_SCOPE)
    set(${cxx_flags} "${_cxx}" PARENT_SCOPE)
endfunction()
