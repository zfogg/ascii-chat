include_guard(GLOBAL)

if(NOT USE_MUSL)
    return()
endif()

include("${CMAKE_SOURCE_DIR}/cmake/toolchains/MuslFlags.cmake")

if(NOT TARGET asciichat-musl-toolchain)
    add_library(asciichat-musl-toolchain INTERFACE)
    asciichat_musl_compile_flags(_asciichat_musl_c_flags _asciichat_musl_cxx_flags)
    separate_arguments(_asciichat_musl_c_flags NATIVE_COMMAND "${_asciichat_musl_c_flags}")
    separate_arguments(_asciichat_musl_cxx_flags NATIVE_COMMAND "${_asciichat_musl_cxx_flags}")
    target_compile_options(asciichat-musl-toolchain INTERFACE
        "$<$<COMPILE_LANGUAGE:C>:${_asciichat_musl_c_flags}>"
        "$<$<COMPILE_LANGUAGE:CXX>:${_asciichat_musl_cxx_flags}>")
    target_link_options(asciichat-musl-toolchain INTERFACE
        "-fuse-ld=lld" "-static-pie" "-nostdlib" "-nostdlib++"
        "-L${MUSL_LIBDIR}" "${MUSL_LIBDIR}/rcrt1.o" "${MUSL_LIBDIR}/crti.o")
    target_link_libraries(asciichat-musl-toolchain INTERFACE
        "-lc" "-lm" "${MUSL_LIBDIR}/crtn.o")
endif()
