include_guard(GLOBAL)

# Single source of truth for dependency projects that cannot consume the
# CMake toolchain target directly (autotools and make-based builds).
set(ASCIICHAT_MUSL_TARGET_FLAG "--target=${MUSL_TARGET}" CACHE INTERNAL
    "Clang target flag shared by musl dependency builds")

# Used by the toolchain and source dependencies after runtime/header staging.
function(asciichat_musl_compile_flags c_flags cxx_flags)
    set(_asciichat_kernel_headers_dir "${KERNEL_HEADERS_DIR}")
    if(NOT _asciichat_kernel_headers_dir AND ASCIICHAT_DEPS_CACHE_DIR)
        set(_asciichat_kernel_headers_dir "${ASCIICHAT_DEPS_CACHE_DIR}/musl/kernel-headers")
    endif()
    set(_c "-fno-lto -ftls-model=global-dynamic -fPIC")
    set(_asciichat_gcc_toolchain FALSE)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
       OR CMAKE_C_COMPILER MATCHES "musl-gcc|musl-gcc-wrapper|(^|/)(gcc|g(cc)?-[0-9]+)$")
        set(_asciichat_gcc_toolchain TRUE)
    endif()
    if(_asciichat_gcc_toolchain)
        string(APPEND _c " -nostdinc -isystem ${MUSL_INCLUDE_DIR}"
            " -isystem ${MUSL_GCC_BUILTIN_INCLUDE_DIR}"
            " -isystem ${MUSL_GCC_FIXED_INCLUDE_DIR}")
    else()
        execute_process(COMMAND "${CMAKE_C_COMPILER}" -print-resource-dir
            OUTPUT_VARIABLE _asciichat_clang_resource_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _asciichat_clang_resource_dir)
            message(FATAL_ERROR "Unable to determine Clang resource include directory")
        endif()
        string(APPEND _c " -nostdlibinc -isystem ${_asciichat_clang_resource_dir}/include -isystem ${MUSL_INCLUDE_DIR}")
    endif()
    if(_asciichat_kernel_headers_dir)
        string(APPEND _c " -isystem ${_asciichat_kernel_headers_dir}")
    endif()
    # C++ must retain the compiler's standard-library include directories. For
    # GCC, -nostdinc would hide libstdc++ headers such as <string>; only the C
    # compiler needs the strict musl header isolation above. Put musl headers
    # after the C++ implementation headers instead.
    # Keep host glibc headers out of C++ dependency builds as well.  libc++
    # must see the staged musl headers exclusively; otherwise a host
    # __mbstate_t declaration can precede musl's and make x265 fail to compile.
    set(_cxx "-nostdinc -fno-lto -ftls-model=global-dynamic -fPIC")
    if(_asciichat_gcc_toolchain)
        string(APPEND _cxx " -nostdinc -nostdinc++"
            " -isystem ${MUSL_GCC_BUILTIN_INCLUDE_DIR}"
            " -isystem ${MUSL_GCC_FIXED_INCLUDE_DIR}")
        if(MUSL_LIBSTDCPP_INCLUDE_DIR AND MUSL_LIBSTDCPP_TARGET_INCLUDE_DIR)
            string(APPEND _cxx " -isystem ${MUSL_LIBSTDCPP_INCLUDE_DIR} -isystem ${MUSL_LIBSTDCPP_TARGET_INCLUDE_DIR}")
        endif()
        string(APPEND _cxx " -isystem ${MUSL_INCLUDE_DIR}")
    else()
        # Clang otherwise appends the host multiarch C headers after the
        # target triple's search path.  Keep libc++ headers enabled while
        # making musl the only C-library header provider.
        string(APPEND _cxx " -nostdlibinc")
    endif()
    if(_asciichat_kernel_headers_dir)
        string(APPEND _cxx " -isystem ${_asciichat_kernel_headers_dir}")
    endif()
    # Some third-party C++ targets inherit the musl flags while selecting a
    # non-libc++ standard library; Clang then reports -stdlib as unused.
    # Keep libc++ selected for the project, but avoid noisy diagnostics from
    # those dependency compile commands.
    if(NOT _asciichat_gcc_toolchain)
        string(APPEND _cxx " -stdlib=libc++ -nostdinc++ -Wno-unused-command-line-argument")
    endif()
    if(ALPINE_LIBCXX_INCLUDE_DIR AND NOT _asciichat_gcc_toolchain)
        string(APPEND _cxx " -isystem ${ALPINE_LIBCXX_INCLUDE_DIR} -D_LIBCPP_HAS_MUSL_LIBC=1")
    else()
        # The musl platform setup supplies the Alpine libc++ headers after
        # the toolchain initializes. Never seed a musl command with the host
        # glibc libc++ tree.
        string(APPEND _cxx " -D_LIBCPP_HAS_MUSL_LIBC=1 -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE=1")
    endif()
    if(NOT _asciichat_gcc_toolchain)
        # Clang's resource headers include a stddef.h of their own.  Keep
        # libc++'s wrapper ahead of that directory so include_next reaches
        # the musl wrapper and defines nullptr_t correctly.
        string(APPEND _cxx " -isystem ${_asciichat_clang_resource_dir}/include")
        string(APPEND _cxx " -isystem ${MUSL_INCLUDE_DIR}")
    endif()
    # Clang diagnoses precedence and duplicate-platform-macro patterns in the
    # external musl headers; keep these diagnostics out of project output.
    if(NOT _asciichat_gcc_toolchain)
        string(APPEND _c " -Wno-bitwise-op-parentheses -Wno-shift-op-parentheses -Wno-macro-redefined")
        string(APPEND _cxx " -Wno-bitwise-op-parentheses -Wno-shift-op-parentheses -Wno-macro-redefined")
    endif()
    if(MUSL_TARGET MATCHES "^aarch64")
        string(APPEND _c " -mno-outline-atomics")
        string(APPEND _cxx " -mno-outline-atomics")
    endif()
    set(${c_flags} "${_c}" PARENT_SCOPE)
    set(${cxx_flags} "${_cxx}" PARENT_SCOPE)
endfunction()
