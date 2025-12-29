# =============================================================================
# RunCMakeWithLog.cmake - Helper script for running cmake configure with log output on failure
# =============================================================================
# This script runs cmake configure, captures output to a log file, and on failure
# outputs the log contents before exiting with the error code.
#
# Required variables:
#   ARGS_FILE              - Path to file containing cmake arguments (one per line)
#   LOG_FILE               - Path to the log file
#   APPEND_TO_LOG          - If TRUE, append to log; if FALSE, overwrite
#   OPERATION_NAME         - Name of the operation for error messages
# =============================================================================

if(NOT DEFINED ARGS_FILE)
    message(FATAL_ERROR "ARGS_FILE not defined")
endif()
if(NOT DEFINED LOG_FILE)
    message(FATAL_ERROR "LOG_FILE not defined")
endif()
if(NOT DEFINED OPERATION_NAME)
    set(OPERATION_NAME "CMake operation")
endif()
if(NOT DEFINED APPEND_TO_LOG)
    set(APPEND_TO_LOG FALSE)
endif()

# Read arguments from file
if(NOT EXISTS "${ARGS_FILE}")
    message(FATAL_ERROR "Args file not found: ${ARGS_FILE}")
endif()
file(STRINGS "${ARGS_FILE}" cmake_args)

# Run cmake configure with the arguments
execute_process(
    COMMAND ${CMAKE_COMMAND} ${cmake_args}
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
