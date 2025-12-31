# ConfigureManPage.cmake
# Build-time script to configure man page template with variable substitution
#
# Required variables:
#   INPUT_FILE  - Path to input template (.1.in)
#   OUTPUT_FILE - Path to output man page (.1)
#   PROJECT_VERSION - Version string to substitute (from PROJECT_VERSION_FROM_GIT)

if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE must be defined")
endif()

if(NOT DEFINED PROJECT_VERSION)
    message(FATAL_ERROR "PROJECT_VERSION must be defined")
endif()

if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Input template file does not exist: ${INPUT_FILE}")
endif()

# Read input template
file(READ "${INPUT_FILE}" CONTENT)

# Perform @VARIABLE@ substitution
string(REPLACE "@PROJECT_VERSION@" "${PROJECT_VERSION}" CONTENT "${CONTENT}")

# Write output file
file(WRITE "${OUTPUT_FILE}" "${CONTENT}")

# Verify output was created
if(NOT EXISTS "${OUTPUT_FILE}")
    message(FATAL_ERROR "Failed to generate man page: ${OUTPUT_FILE}")
endif()

message(STATUS "Man page configured successfully: ${OUTPUT_FILE}")
