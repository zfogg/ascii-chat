# =============================================================================
# RunCMakeBuildWithLog.cmake - Helper script for running cmake build with log output on failure
# =============================================================================
# This script runs cmake --build, captures output to a log file, and on failure
# outputs the log contents before exiting with the error code.
#
# Required variables:
#   BUILD_DIR              - Path to the build directory
#   BUILD_TARGET           - Target to build (e.g., "generate_version")
#   LOG_FILE               - Path to the log file
#   APPEND_TO_LOG          - If TRUE, append to log; if FALSE, overwrite
#   OPERATION_NAME         - Name of the operation for error messages
# =============================================================================

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR not defined")
endif()
if(NOT DEFINED BUILD_TARGET)
    message(FATAL_ERROR "BUILD_TARGET not defined")
endif()
if(NOT DEFINED LOG_FILE)
    message(FATAL_ERROR "LOG_FILE not defined")
endif()
if(NOT DEFINED OPERATION_NAME)
    set(OPERATION_NAME "CMake build")
endif()
if(NOT DEFINED APPEND_TO_LOG)
    set(APPEND_TO_LOG FALSE)
endif()

# Run cmake build
execute_process(
    COMMAND ${CMAKE_COMMAND} --build "${BUILD_DIR}" --target "${BUILD_TARGET}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)

# Combine stdout and stderr
set(combined_output "${stdout}\n${stderr}")

# Write to log file
if(APPEND_TO_LOG)
    file(APPEND "${LOG_FILE}" "${combined_output}\n")
else()
    file(WRITE "${LOG_FILE}" "${combined_output}\n")
endif()

# Check result
if(NOT result EQUAL 0)
    message(STATUS "")
    message(STATUS "=============================================================")
    message(STATUS "=== ${OPERATION_NAME} FAILED (exit code: ${result}) ===")
    message(STATUS "=============================================================")
    message(STATUS "Log file: ${LOG_FILE}")
    message(STATUS "")
    # Read and print log file
    if(EXISTS "${LOG_FILE}")
        file(READ "${LOG_FILE}" log_contents)
        message(STATUS "${log_contents}")
    else()
        message(STATUS "(Log file not found)")
    endif()
    message(STATUS "")
    message(STATUS "=== End of log ===")
    message(FATAL_ERROR "${OPERATION_NAME} failed with exit code ${result}")
endif()
