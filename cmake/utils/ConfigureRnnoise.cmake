if(NOT DEFINED RNNOISE_SOURCE_DIR OR NOT DEFINED RNNOISE_BINARY_DIR OR NOT DEFINED RNNOISE_INSTALL_DIR OR NOT DEFINED RNNOISE_SHELL)
    message(FATAL_ERROR "RNNoise configure helper requires source, binary, install, and shell paths")
endif()

file(MAKE_DIRECTORY "${RNNOISE_BINARY_DIR}")
file(COPY "${RNNOISE_SOURCE_DIR}/" DESTINATION "${RNNOISE_BINARY_DIR}")

if(RNNOISE_SHARED)
    set(_rnnoise_link_flags "--disable-static --enable-shared")
else()
    set(_rnnoise_link_flags "--disable-shared --enable-static")
endif()

execute_process(
    COMMAND "${RNNOISE_SHELL}" -c "./autogen.sh && ./configure --prefix='${RNNOISE_INSTALL_DIR}' --disable-examples --disable-doc ${_rnnoise_link_flags}"
    WORKING_DIRECTORY "${RNNOISE_BINARY_DIR}"
    RESULT_VARIABLE _rnnoise_result
)
if(NOT _rnnoise_result EQUAL 0)
    message(FATAL_ERROR "RNNoise autotools configuration failed (${_rnnoise_result})")
endif()
