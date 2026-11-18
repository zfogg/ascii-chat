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

if(WIN32)
    # Do not add the ELF-only -fPIC flag to the native Windows autotools build,
    # and prevent a stale process-level CFLAGS value from being forwarded.
    set(ENV{CFLAGS} "")
    set(_rnnoise_build_command
        "${CMAKE_COMMAND}" -E env
        "CFLAGS=-DM_PI=3.14159265358979323846"
        "${ASCIICHAT_MAKE_EXECUTABLE}" -C "${ASCIICHAT_RNNOISE_BUILD_DIR}")
else()
    set(_rnnoise_build_command
        "${ASCIICHAT_MAKE_EXECUTABLE}" -C "${ASCIICHAT_RNNOISE_BUILD_DIR}")
endif()

ExternalProject_Add(asciichat-rnnoise-build
    SOURCE_DIR "${ASCIICHAT_RNNOISE_SOURCE_DIR}"
    BINARY_DIR "${ASCIICHAT_RNNOISE_BUILD_DIR}"
    CONFIGURE_COMMAND "${CMAKE_COMMAND}"
        -DRNNOISE_SOURCE_DIR=${ASCIICHAT_RNNOISE_SOURCE_DIR}
        -DRNNOISE_BINARY_DIR=${ASCIICHAT_RNNOISE_BUILD_DIR}
        -DRNNOISE_INSTALL_DIR=${ASCIICHAT_RNNOISE_INSTALL_DIR}
        -DRNNOISE_SHELL=${ASCIICHAT_BASH_EXECUTABLE}
        -DRNNOISE_CC=${ASCIICHAT_CLANG_EXECUTABLE}
        -DRNNOISE_SHARED=OFF
        -P "${CMAKE_SOURCE_DIR}/cmake/utils/ConfigureRnnoise.cmake"
    BUILD_COMMAND ${_rnnoise_build_command}
    INSTALL_COMMAND "${ASCIICHAT_MAKE_EXECUTABLE}" -C "${ASCIICHAT_RNNOISE_BUILD_DIR}" install
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
unset(_rnnoise_build_command)
