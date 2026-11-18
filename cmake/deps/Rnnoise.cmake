# RNNoise is an autotools project. Build it with its upstream build system and
# expose only the installed library/header to ascii-chat targets.
include(ExternalProject)

set(ASCIICHAT_RNNOISE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise")
set(ASCIICHAT_RNNOISE_BUILD_DIR "${CMAKE_BINARY_DIR}/rnnoise-build")
set(ASCIICHAT_RNNOISE_INSTALL_DIR "${CMAKE_BINARY_DIR}/rnnoise-install")

# Imported targets validate their interface include directories during CMake
# generation. RNNoise creates this directory during ExternalProject install,
# so create the empty staging directory up front as well.
file(MAKE_DIRECTORY "${ASCIICHAT_RNNOISE_INSTALL_DIR}/include")

find_program(ASCIICHAT_RNNOISE_SHELL NAMES bash sh
    PATHS "C:/msys64/usr/bin" "C:/msys64/bin" NO_DEFAULT_PATH)
if(NOT ASCIICHAT_RNNOISE_SHELL)
    find_program(ASCIICHAT_RNNOISE_SHELL NAMES bash sh)
endif()
find_program(ASCIICHAT_RNNOISE_MAKE NAMES make gmake
    PATHS "C:/msys64/usr/bin" "C:/msys64/bin" NO_DEFAULT_PATH)
if(NOT ASCIICHAT_RNNOISE_MAKE)
    find_program(ASCIICHAT_RNNOISE_MAKE NAMES make gmake)
endif()
if(NOT ASCIICHAT_RNNOISE_SHELL OR NOT ASCIICHAT_RNNOISE_MAKE)
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
if(NOT CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  list(APPEND _rnnoise_cc_args -DRNNOISE_CC=${ASCIICHAT_CLANG_EXECUTABLE})
endif()

ExternalProject_Add(ascii-chat-rnnoise-build
    SOURCE_DIR "${ASCIICHAT_RNNOISE_SOURCE_DIR}"
    BINARY_DIR "${ASCIICHAT_RNNOISE_BUILD_DIR}"
    CONFIGURE_COMMAND "${CMAKE_COMMAND}"
        -DRNNOISE_SOURCE_DIR=${ASCIICHAT_RNNOISE_SOURCE_DIR}
        -DRNNOISE_BINARY_DIR=${ASCIICHAT_RNNOISE_BUILD_DIR}
        -DRNNOISE_INSTALL_DIR=${ASCIICHAT_RNNOISE_INSTALL_DIR}
        -DRNNOISE_SHELL=${ASCIICHAT_RNNOISE_SHELL}
        ${_rnnoise_cc_args}
        -DRNNOISE_SHARED=OFF
        -P "${CMAKE_SOURCE_DIR}/cmake/utils/ConfigureRnnoise.cmake"
    # configure/autogen has already generated aclocal.m4; prevent make from
    # requiring the exact Automake version used by the source checkout.
    BUILD_COMMAND "${ASCIICHAT_RNNOISE_MAKE}" -C "${ASCIICHAT_RNNOISE_BUILD_DIR}" -o aclocal.m4 -o Makefile.in
        CFLAGS=-DM_PI=3.14159265358979323846
    INSTALL_COMMAND "${ASCIICHAT_RNNOISE_MAKE}" -C "${ASCIICHAT_RNNOISE_BUILD_DIR}" install
    BUILD_BYPRODUCTS "${_rnnoise_library}"
    BUILD_ALWAYS FALSE
    UPDATE_COMMAND ""
    TEST_COMMAND ""
    INSTALL_DIR "${ASCIICHAT_RNNOISE_INSTALL_DIR}"
)

add_library(ascii-chat-rnnoise ${_rnnoise_library_type} IMPORTED GLOBAL)
set_target_properties(ascii-chat-rnnoise PROPERTIES
    IMPORTED_LOCATION "${_rnnoise_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${ASCIICHAT_RNNOISE_INSTALL_DIR}/include"
)
if(_rnnoise_import_library)
    set_target_properties(ascii-chat-rnnoise PROPERTIES IMPORTED_IMPLIB "${_rnnoise_import_library}")
endif()
add_dependencies(ascii-chat-rnnoise ascii-chat-rnnoise-build)

unset(_rnnoise_library_type)
unset(_rnnoise_library)
unset(_rnnoise_import_library)
unset(_rnnoise_cc_args)
