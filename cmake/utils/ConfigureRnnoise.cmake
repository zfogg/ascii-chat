if(NOT DEFINED RNNOISE_SOURCE_DIR OR NOT DEFINED RNNOISE_BINARY_DIR OR NOT DEFINED RNNOISE_INSTALL_DIR OR NOT DEFINED RNNOISE_SHELL)
    message(FATAL_ERROR "RNNoise configure helper requires source, binary, install, and shell paths")
endif()

file(MAKE_DIRECTORY "${RNNOISE_BINARY_DIR}")
file(COPY "${RNNOISE_SOURCE_DIR}/" DESTINATION "${RNNOISE_BINARY_DIR}")

if(DEFINED RNNOISE_CC)
    set(ENV{CC} "${RNNOISE_CC}")
endif()
set(ENV{CFLAGS} "$ENV{CFLAGS} -DM_PI=3.14159265358979323846 -fPIC")

if(RNNOISE_SHARED)
    set(_rnnoise_link_flags "--disable-static --enable-shared")
else()
    set(_rnnoise_link_flags "--disable-shared --enable-static")
endif()

if(WIN32)
    # MSYS2 login shells do not inherit the Windows UCRT64 toolchain path.
    # Convert only Windows paths; native Unix shells accept native paths.
    set(_rnnoise_shell_command
        "export PATH=\"/ucrt64/bin:/clang64/bin:/usr/local/bin:/usr/bin:$PATH\" && cd \"$(cygpath -u '${RNNOISE_BINARY_DIR}')\" && ./autogen.sh && ./configure --prefix=\"$(cygpath -u '${RNNOISE_INSTALL_DIR}')\" --disable-examples --disable-doc ${_rnnoise_link_flags}")
else()
    set(_rnnoise_shell_command
        "cd '${RNNOISE_BINARY_DIR}' && ./autogen.sh && ./configure --prefix='${RNNOISE_INSTALL_DIR}' --disable-examples --disable-doc ${_rnnoise_link_flags}")
endif()

execute_process(
    COMMAND "${RNNOISE_SHELL}" -lc "${_rnnoise_shell_command}"
    WORKING_DIRECTORY "${RNNOISE_BINARY_DIR}"
    RESULT_VARIABLE _rnnoise_result
)
if(NOT _rnnoise_result EQUAL 0)
    message(FATAL_ERROR "RNNoise autotools configuration failed (${_rnnoise_result})")
endif()
