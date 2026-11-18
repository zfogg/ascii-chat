if(NOT DEFINED RNNOISE_SOURCE_DIR OR NOT DEFINED RNNOISE_BINARY_DIR OR NOT DEFINED RNNOISE_INSTALL_DIR OR NOT DEFINED RNNOISE_SHELL)
    message(FATAL_ERROR "RNNoise configure helper requires source, binary, install, and shell paths")
endif()

# Some external-project generators leave the optional shell value empty on
# Unix.  Do not try to execute an empty command; use the user's POSIX shell.
if(RNNOISE_SHELL)
    set(_rnnoise_shell_path "${RNNOISE_SHELL}")
else()
    find_program(_rnnoise_shell_path NAMES zsh bash sh REQUIRED)
endif()

file(MAKE_DIRECTORY "${RNNOISE_BINARY_DIR}")
file(COPY "${RNNOISE_SOURCE_DIR}/" DESTINATION "${RNNOISE_BINARY_DIR}")

if(DEFINED RNNOISE_CC)
    set(ENV{CC} "${RNNOISE_CC}")
endif()
set(_rnnoise_cflags "$ENV{CFLAGS}")
if(NOT RNNOISE_WINDOWS)
    string(APPEND _rnnoise_cflags " -fPIC")
endif()
set(ENV{CFLAGS} "${_rnnoise_cflags}")
set(ENV{CPPFLAGS} "-DM_PI=3.14159265358979323846")
# Do not leak the parent application's Homebrew or iOS link flags into the
# autotools probe.  The dependency's compiler wrapper owns its target flags.
set(ENV{LDFLAGS} "")

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
    COMMAND "${_rnnoise_shell_path}" -lc "${_rnnoise_shell_command}"
    WORKING_DIRECTORY "${RNNOISE_BINARY_DIR}"
    RESULT_VARIABLE _rnnoise_result
)
if(NOT _rnnoise_result EQUAL 0)
    message(FATAL_ERROR "RNNoise autotools configuration failed (${_rnnoise_result})")
endif()

# The configure step has already produced the generated autotools inputs.
# Automake otherwise re-enters aclocal/autoheader during a later build when
# their source timestamps differ, which is not required for a fixed checkout
# and is unavailable in a minimal Windows MSYS installation.
set(_rnnoise_makefile "${RNNOISE_BINARY_DIR}/Makefile")
if(EXISTS "${_rnnoise_makefile}")
    file(READ "${_rnnoise_makefile}" _rnnoise_makefile_contents)
    string(REPLACE "$(ACLOCAL_M4):  $(am__aclocal_m4_deps)"
        "$(ACLOCAL_M4):" _rnnoise_makefile_contents "${_rnnoise_makefile_contents}")
    string(REPLACE "$(srcdir)/config.h.in:  $(am__configure_deps) "
        "$(srcdir)/config.h.in:" _rnnoise_makefile_contents "${_rnnoise_makefile_contents}")
    string(REPLACE "$(srcdir)/Makefile.in:  $(srcdir)/Makefile.am  $(am__configure_deps)"
        "$(srcdir)/Makefile.in:  $(srcdir)/Makefile.am"
        _rnnoise_makefile_contents "${_rnnoise_makefile_contents}")
    string(REPLACE "$(top_srcdir)/configure:  $(am__configure_deps)"
        "$(top_srcdir)/configure:" _rnnoise_makefile_contents "${_rnnoise_makefile_contents}")
    file(WRITE "${_rnnoise_makefile}" "${_rnnoise_makefile_contents}")
endif()
