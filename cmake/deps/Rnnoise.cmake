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

find_program(ASCIICHAT_RNNOISE_SHELL NAMES bash sh)
find_program(ASCIICHAT_RNNOISE_MAKE NAMES make gmake)
if(NOT ASCIICHAT_RNNOISE_SHELL OR NOT ASCIICHAT_RNNOISE_MAKE)
    message(FATAL_ERROR "RNNoise requires bash/sh and make to follow its upstream autotools build")
endif()

if(BUILD_SHARED_LIBS)
    set(_rnnoise_library_type SHARED)
    if(WIN32)
        set(_rnnoise_library "${ASCIICHAT_RNNOISE_INSTALL_DIR}/bin/rnnoise-0.dll")
        set(_rnnoise_import_library "${ASCIICHAT_RNNOISE_INSTALL_DIR}/lib/librnnoise.dll.a")
    elseif(APPLE)
        set(_rnnoise_library "${ASCIICHAT_RNNOISE_INSTALL_DIR}/lib/librnnoise.dylib")
    else()
        set(_rnnoise_library "${ASCIICHAT_RNNOISE_INSTALL_DIR}/lib/librnnoise.so")
    endif()
else()
    set(_rnnoise_library_type STATIC)
    set(_rnnoise_library "${ASCIICHAT_RNNOISE_INSTALL_DIR}/lib/librnnoise.a")
endif()

ExternalProject_Add(ascii-chat-rnnoise-build
    SOURCE_DIR "${ASCIICHAT_RNNOISE_SOURCE_DIR}"
    BINARY_DIR "${ASCIICHAT_RNNOISE_BUILD_DIR}"
    CONFIGURE_COMMAND "${CMAKE_COMMAND}"
        -DRNNOISE_SOURCE_DIR=${ASCIICHAT_RNNOISE_SOURCE_DIR}
        -DRNNOISE_BINARY_DIR=${ASCIICHAT_RNNOISE_BUILD_DIR}
        -DRNNOISE_INSTALL_DIR=${ASCIICHAT_RNNOISE_INSTALL_DIR}
        -DRNNOISE_SHELL=${ASCIICHAT_RNNOISE_SHELL}
        -DRNNOISE_SHARED=${BUILD_SHARED_LIBS}
        -P "${CMAKE_SOURCE_DIR}/cmake/utils/ConfigureRnnoise.cmake"
    BUILD_COMMAND "${ASCIICHAT_RNNOISE_MAKE}" -C "${ASCIICHAT_RNNOISE_BUILD_DIR}"
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
