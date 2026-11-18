include_guard(GLOBAL)

# Host tools must never inherit the target sysroot or musl linker flags.
function(asciichat_host_toolchain out_c out_cxx out_cache)
    if(NOT ASCIICHAT_CLANG_EXECUTABLE)
        find_program(ASCIICHAT_CLANG_EXECUTABLE NAMES clang-23 clang REQUIRED)
    endif()
    if(NOT ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE)
        find_program(ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE NAMES clang++-23 clang++ REQUIRED)
    endif()
    set(${out_c} "${ASCIICHAT_CLANG_EXECUTABLE}" PARENT_SCOPE)
    set(${out_cxx} "${ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE}" PARENT_SCOPE)
    set(${out_cache} "${ASCIICHAT_DEPS_CACHE_DIR}/host-tools" PARENT_SCOPE)
endfunction()
