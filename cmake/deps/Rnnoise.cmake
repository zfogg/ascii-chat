# RNNoise is an autotools project. Build it with its upstream build system and
# expose only the installed library/header to ascii-chat targets.
include(ExternalProject)

set(ASCIICHAT_RNNOISE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise")
set(ASCIICHAT_RNNOISE_BUILD_DIR "${CMAKE_BINARY_DIR}/rnnoise-build")
set(ASCIICHAT_RNNOISE_INSTALL_DIR "${CMAKE_BINARY_DIR}/rnnoise-install")

# Reuse the centrally discovered shell.  Passing an undefined RNNOISE_SHELL
# through ExternalProject otherwise produces an empty command on Windows.
if(NOT ASCIICHAT_RNNOISE_SHELL)
    set(ASCIICHAT_RNNOISE_SHELL "${ASCIICHAT_BASH_EXECUTABLE}")
endif()

# Imported targets validate their interface include directories during CMake
# generation. RNNoise creates this directory during ExternalProject install,
# so create the empty staging directory up front as well.
file(MAKE_DIRECTORY "${ASCIICHAT_RNNOISE_INSTALL_DIR}/include")

if(NOT ASCIICHAT_RNNOISE_SHELL)
    find_program(ASCIICHAT_RNNOISE_SHELL NAMES bash sh REQUIRED)
endif()
if(NOT ASCIICHAT_RNNOISE_MAKE)
    find_program(ASCIICHAT_RNNOISE_MAKE NAMES make gmake REQUIRED)
endif()
if(NOT ASCIICHAT_BASH_EXECUTABLE OR NOT ASCIICHAT_MAKE_EXECUTABLE)
    message(FATAL_ERROR "RNNoise requires bash/sh and make to follow its upstream autotools build")
endif()

# RNNoise is an internal dependency. Build it statically even when the main
# application is shared; this avoids autotools' GNU/Windows DLL export flags
# and keeps the dependency inside asciichat's shared library.
set(_rnnoise_library_type STATIC)
if(WIN32)
  set(_rnnoise_library "${ASCIICHAT_RNNOISE_INSTALL_DIR}/lib/rnnoise.lib")
else()
  set(_rnnoise_library "${ASCIICHAT_RNNOISE_INSTALL_DIR}/lib/librnnoise.a")
endif()

set(_rnnoise_cc_args)
if(WIN32)
  set(_rnnoise_cc_wrapper "${CMAKE_CURRENT_BINARY_DIR}/rnnoise-clang.sh")
  configure_file(
      "${CMAKE_SOURCE_DIR}/cmake/utils/RnnoiseClang.sh.in"
      "${_rnnoise_cc_wrapper}"
      @ONLY)
  file(CHMOD "${_rnnoise_cc_wrapper}"
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
  set(_rnnoise_cc "${_rnnoise_cc_wrapper}")
  list(APPEND _rnnoise_cc_args -DRNNOISE_CC=${_rnnoise_cc})
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
  list(APPEND _rnnoise_cc_args -DRNNOISE_CC=${ASCIICHAT_CLANG_EXECUTABLE})
endif()

if(WIN32)
    # Do not add the ELF-only -fPIC flag to the native Windows autotools build,
    # and prevent a stale process-level CFLAGS value from being forwarded.
    set(ENV{CFLAGS} "")
    set(_rnnoise_build_command
        "${CMAKE_COMMAND}" -E env
        "CFLAGS=-DM_PI=3.14159265358979323846"
        "MSYS_NO_PATHCONV=1"
        "${ASCIICHAT_MAKE_EXECUTABLE}" -C "${ASCIICHAT_RNNOISE_BUILD_DIR}")
else()
    set(_rnnoise_build_command
        "${ASCIICHAT_MAKE_EXECUTABLE}" -C "${ASCIICHAT_RNNOISE_BUILD_DIR}")
endif()

ExternalProject_Add(ascii-chat-rnnoise-build
    SOURCE_DIR "${ASCIICHAT_RNNOISE_SOURCE_DIR}"
    BINARY_DIR "${ASCIICHAT_RNNOISE_BUILD_DIR}"
    CONFIGURE_COMMAND "${CMAKE_COMMAND}"
        -DRNNOISE_SOURCE_DIR=${ASCIICHAT_RNNOISE_SOURCE_DIR}
        -DRNNOISE_BINARY_DIR=${ASCIICHAT_RNNOISE_BUILD_DIR}
        -DRNNOISE_INSTALL_DIR=${ASCIICHAT_RNNOISE_INSTALL_DIR}
        -DRNNOISE_SHELL=${ASCIICHAT_RNNOISE_SHELL}
        -DRNNOISE_WINDOWS=${WIN32}
        -DRNNOISE_LLVM_LIB=${ASCIICHAT_LLVM_LIB_EXECUTABLE}
        ${_rnnoise_cc_args}
        -DRNNOISE_SHARED=OFF
        -P "${CMAKE_SOURCE_DIR}/cmake/utils/ConfigureRnnoise.cmake"
    # configure/autogen has already generated aclocal.m4; prevent make from
    # requiring the exact Automake version used by the source checkout.
    BUILD_COMMAND "${CMAKE_COMMAND}" -E touch
        "${ASCIICHAT_RNNOISE_BUILD_DIR}/aclocal.m4"
        "${ASCIICHAT_RNNOISE_BUILD_DIR}/Makefile.in"
        "${ASCIICHAT_RNNOISE_BUILD_DIR}/configure"
        "${ASCIICHAT_RNNOISE_BUILD_DIR}/config.h.in"
        "${ASCIICHAT_RNNOISE_BUILD_DIR}/config.status"
        "${ASCIICHAT_RNNOISE_BUILD_DIR}/Makefile"
        "${ASCIICHAT_RNNOISE_BUILD_DIR}/config.h"
        "${ASCIICHAT_RNNOISE_BUILD_DIR}/stamp-h1"
        "${ASCIICHAT_RNNOISE_BUILD_DIR}/rnnoise.pc"
        "${ASCIICHAT_RNNOISE_BUILD_DIR}/rnnoise-uninstalled.pc"
        COMMAND "${CMAKE_COMMAND}" -E env
        "MAKEFLAGS=-o aclocal.m4 -o Makefile.in -o configure -o config.h.in"
        "MSYS_NO_PATHCONV=1"
        "${ASCIICHAT_RNNOISE_MAKE}" -o aclocal.m4 -o Makefile.in -o configure -o config.h.in
        -C "${ASCIICHAT_RNNOISE_BUILD_DIR}"
    INSTALL_COMMAND "${ASCIICHAT_RNNOISE_MAKE}" -C "${ASCIICHAT_RNNOISE_BUILD_DIR}" install
        COMMAND "${CMAKE_COMMAND}"
            -DRNNOISE_BUILD_DIR=${ASCIICHAT_RNNOISE_BUILD_DIR}
            -DRNNOISE_INSTALL_DIR=${ASCIICHAT_RNNOISE_INSTALL_DIR}
            -DRNNOISE_LLVM_LIB=${ASCIICHAT_LLVM_LIB_EXECUTABLE}
            -P "${CMAKE_SOURCE_DIR}/cmake/utils/InstallRnnoise.cmake"
    BUILD_BYPRODUCTS "${_rnnoise_library}"
    BUILD_ALWAYS FALSE
    UPDATE_COMMAND ""
    TEST_COMMAND ""
    INSTALL_DIR "${ASCIICHAT_RNNOISE_INSTALL_DIR}"
)

add_library(asciichat-rnnoise ${_rnnoise_library_type} IMPORTED GLOBAL)
set_target_properties(asciichat-rnnoise PROPERTIES
    IMPORTED_LOCATION "${_rnnoise_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${ASCIICHAT_RNNOISE_INSTALL_DIR}/include"
)
if(_rnnoise_import_library)
    set_target_properties(asciichat-rnnoise PROPERTIES IMPORTED_IMPLIB "${_rnnoise_import_library}")
endif()
add_dependencies(asciichat-rnnoise asciichat-rnnoise-build)

unset(_rnnoise_library_type)
unset(_rnnoise_library)
unset(_rnnoise_import_library)
unset(_rnnoise_cc_args)
unset(_rnnoise_cc_wrapper)
unset(_rnnoise_build_command)
