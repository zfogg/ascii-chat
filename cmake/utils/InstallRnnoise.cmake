if(NOT WIN32)
    return()
endif()

if(NOT RNNOISE_BUILD_DIR OR NOT RNNOISE_INSTALL_DIR OR NOT RNNOISE_LLVM_LIB)
    message(FATAL_ERROR "RNNoise MSVC install requires build, install, and llvm-lib paths")
endif()

file(GLOB _rnnoise_objects CONFIGURE_DEPENDS "${RNNOISE_BUILD_DIR}/.libs/*.o")
if(NOT _rnnoise_objects)
    message(FATAL_ERROR "RNNoise object files were not produced in ${RNNOISE_BUILD_DIR}/.libs")
endif()

file(MAKE_DIRECTORY "${RNNOISE_INSTALL_DIR}/lib")
execute_process(
    COMMAND "${RNNOISE_LLVM_LIB}" "/out:${RNNOISE_INSTALL_DIR}/lib/rnnoise.lib" ${_rnnoise_objects}
    RESULT_VARIABLE _rnnoise_result
    OUTPUT_VARIABLE _rnnoise_output
    ERROR_VARIABLE _rnnoise_error)
if(NOT _rnnoise_result EQUAL 0)
    message(FATAL_ERROR "llvm-lib failed to create rnnoise.lib: ${_rnnoise_output}${_rnnoise_error}")
endif()
