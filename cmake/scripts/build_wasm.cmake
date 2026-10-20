if(NOT WASM_BUILD_DIR OR NOT WASM_TARGETS)
    message(FATAL_ERROR "WASM_BUILD_DIR and WASM_TARGETS are required")
endif()

file(LOCK "${WASM_BUILD_DIR}/.asciichat-build.lock"
    GUARD PROCESS TIMEOUT 1800 RESULT_VARIABLE _lock_result)
if(NOT _lock_result STREQUAL "0")
    message(FATAL_ERROR "Cannot lock WASM build directory: ${_lock_result}")
endif()

set(_config_args)
if(WASM_CONFIG)
    list(APPEND _config_args --config "${WASM_CONFIG}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${WASM_BUILD_DIR}"
        ${_config_args} --target ${WASM_TARGETS}
    RESULT_VARIABLE _build_result)
if(NOT _build_result STREQUAL "0")
    message(FATAL_ERROR "WASM build failed: ${_build_result}")
endif()
