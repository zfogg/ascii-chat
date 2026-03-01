# =============================================================================
# ConfigureTemplate.cmake - Simple Template Configuration
# =============================================================================
#
# Configures a template file by substituting CMake variables.
# Used for man page templates to inject version information at build time.
#

if(NOT DEFINED PROJECT_VERSION)
    message(FATAL_ERROR "ConfigureTemplate: PROJECT_VERSION not defined")
endif()

if(NOT DEFINED PROJECT_VERSION_DATE)
    message(FATAL_ERROR "ConfigureTemplate: PROJECT_VERSION_DATE not defined")
endif()

# Extract copyright year from PROJECT_VERSION_DATE (format: YYYY-MM-DD)
string(SUBSTRING "${PROJECT_VERSION_DATE}" 0 4 COPYRIGHT_YEAR_END)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR not defined")
endif()
if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR not defined")
endif()

set(SOURCE_FILE "${SOURCE_DIR}/share/man/man1/ascii-chat.1.in")
set(OUTPUT_FILE "${BINARY_DIR}/share/man/man1/ascii-chat.1.in")

file(MAKE_DIRECTORY "${BINARY_DIR}/share/man/man1")

# Verify source file exists before attempting configuration
if(NOT EXISTS "${SOURCE_FILE}")
    message(FATAL_ERROR "Source template file not found: ${SOURCE_FILE}")
endif()

# Configure the template using @VARIABLE@ substitution
configure_file("${SOURCE_FILE}" "${OUTPUT_FILE}" @ONLY)
