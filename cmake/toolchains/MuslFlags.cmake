include_guard(GLOBAL)

# Used by the toolchain and source dependencies after runtime/header staging.
function(asciichat_musl_compile_flags c_flags cxx_flags)
    set(_c "-fno-lto -ftls-model=global-dynamic -fPIC")
    set(_asciichat_gcc_toolchain FALSE)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
       OR CMAKE_C_COMPILER MATCHES "musl-gcc|musl-gcc-wrapper|(^|/)(gcc|g(cc)?-[0-9]+)$")
        set(_asciichat_gcc_toolchain TRUE)
    endif()
    if(_asciichat_gcc_toolchain)
        if(ASCIICHAT_DEPS_CACHE_DIR AND (NOT EXISTS "${MUSL_LIBSTDCPP_INCLUDE_DIR}/string"
                OR NOT IS_DIRECTORY "${MUSL_LIBSTDCPP_TARGET_INCLUDE_DIR}"))
            file(GLOB _asciichat_gcc_stdcpp_roots LIST_DIRECTORIES true
                "${ASCIICHAT_DEPS_CACHE_DIR}/alpine-libstdcxx/usr/include/c++/*")
            list(SORT _asciichat_gcc_stdcpp_roots)
            list(GET _asciichat_gcc_stdcpp_roots 0 MUSL_LIBSTDCPP_INCLUDE_DIR)
            file(GLOB _asciichat_gcc_target_roots LIST_DIRECTORIES true
                "${MUSL_LIBSTDCPP_INCLUDE_DIR}/*-linux-musl")
            list(SORT _asciichat_gcc_target_roots)
            list(GET _asciichat_gcc_target_roots 0 MUSL_LIBSTDCPP_TARGET_INCLUDE_DIR)
        endif()
        string(APPEND _c " -nostdinc -isystem ${MUSL_INCLUDE_DIR}"
            " -isystem ${MUSL_GCC_BUILTIN_INCLUDE_DIR}"
            " -isystem ${MUSL_GCC_FIXED_INCLUDE_DIR}")
    else()
        string(APPEND _c " -nostdlibinc -isystem ${MUSL_INCLUDE_DIR}")
    endif()
    if(KERNEL_HEADERS_DIR)
        string(APPEND _c " -isystem ${KERNEL_HEADERS_DIR}")
    endif()
    # C++ must retain the compiler's standard-library include directories. For
    # GCC, -nostdinc would hide libstdc++ headers such as <string>; only the C
    # compiler needs the strict musl header isolation above. Put musl headers
    # after the C++ implementation headers instead.
    set(_cxx "-fno-lto -ftls-model=global-dynamic -fPIC")
    if(_asciichat_gcc_toolchain)
        string(APPEND _cxx " -nostdinc -nostdinc++")
        string(APPEND _cxx " -isystem ${MUSL_INCLUDE_DIR}"
            " -isystem ${MUSL_GCC_BUILTIN_INCLUDE_DIR}"
            " -isystem ${MUSL_GCC_FIXED_INCLUDE_DIR}")
    else()
        string(APPEND _cxx " -idirafter ${MUSL_INCLUDE_DIR}")
    endif()
    if(_asciichat_gcc_toolchain AND MUSL_LIBSTDCPP_INCLUDE_DIR AND MUSL_LIBSTDCPP_TARGET_INCLUDE_DIR)
        string(APPEND _cxx " -isystem ${MUSL_LIBSTDCPP_INCLUDE_DIR} -isystem ${MUSL_LIBSTDCPP_TARGET_INCLUDE_DIR}")
    endif()
    if(KERNEL_HEADERS_DIR)
        string(APPEND _cxx " -idirafter ${KERNEL_HEADERS_DIR}")
    endif()
    # Some third-party C++ targets inherit the musl flags while selecting a
    # non-libc++ standard library; Clang then reports -stdlib as unused.
    # Keep libc++ selected for the project, but avoid noisy diagnostics from
    # those dependency compile commands.
    if(NOT _asciichat_gcc_toolchain)
        string(APPEND _cxx " -stdlib=libc++ -nostdinc++ -Wno-unused-command-line-argument")
    endif()
    # Clang diagnoses precedence and duplicate-platform-macro patterns in the
    # external musl headers; keep these diagnostics out of project output.
    if(NOT _asciichat_gcc_toolchain)
        string(APPEND _c " -Wno-bitwise-op-parentheses -Wno-shift-op-parentheses -Wno-macro-redefined")
        string(APPEND _cxx " -Wno-bitwise-op-parentheses -Wno-shift-op-parentheses -Wno-macro-redefined")
    endif()
    if(ALPINE_LIBCXX_INCLUDE_DIR AND NOT _asciichat_gcc_toolchain)
        string(APPEND _cxx " -isystem ${ALPINE_LIBCXX_INCLUDE_DIR}")
    endif()
    if(MUSL_TARGET MATCHES "^aarch64")
        string(APPEND _c " -mno-outline-atomics")
        string(APPEND _cxx " -mno-outline-atomics")
    endif()
    set(${c_flags} "${_c}" PARENT_SCOPE)
    set(${cxx_flags} "${_cxx}" PARENT_SCOPE)
endfunction()
