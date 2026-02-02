# =============================================================================
# Configure Man5 Template Helper Script
# =============================================================================
# This script processes the man5 template with CMake variables.
#
# Required variables:
#   SOURCE_DIR: Project source directory
#   BINARY_DIR: Project binary directory
#   PROJECT_VERSION: Project version (e.g., 1.2.3)
#   PROJECT_VERSION_DATE: Project version date
# =============================================================================

# Validate required variables
if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR not specified")
endif()

if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR not specified")
endif()

if(NOT DEFINED PROJECT_VERSION)
    message(FATAL_ERROR "PROJECT_VERSION not specified")
endif()

if(NOT DEFINED PROJECT_VERSION_DATE)
    message(FATAL_ERROR "PROJECT_VERSION_DATE not specified")
endif()

# Set input and output file paths
set(INPUT_FILE "${SOURCE_DIR}/share/man/man5/ascii-chat.5.in")
set(OUTPUT_FILE "${BINARY_DIR}/share/man/man5/ascii-chat.5")

# Validate input file exists
if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Template file not found: ${INPUT_FILE}")
endif()

# Ensure output directory exists
file(MAKE_DIRECTORY "${BINARY_DIR}/share/man/man5")

# Configure the template
configure_file(
    "${INPUT_FILE}"
    "${OUTPUT_FILE}"
    @ONLY
)

message(STATUS "Generated man5 page: ${OUTPUT_FILE}")
