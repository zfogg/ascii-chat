# =============================================================================
# Fix Compilation Database Directory Field
# =============================================================================
# Post-processes compile_commands.json to fix the "directory" field.
# CMAKE_EXPORT_COMPILE_COMMANDS uses the build directory as the "directory"
# field, but LibTooling-based tools like the defer tool need it to be set
# to the source directory so that paths can be resolved correctly.
#
# This script replaces all "directory" fields with the source directory path.

if(NOT INPUT_FILE)
    message(FATAL_ERROR "FixCompilationDBDirectory: INPUT_FILE not specified")
endif()

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "FixCompilationDBDirectory: SOURCE_DIR not specified")
endif()

# Read the entire file
file(READ "${INPUT_FILE}" _db_content)

# Replace all "directory" fields with the source directory
# This regex matches: "directory": "..something.."
# And replaces it with: "directory": "..SOURCE_DIR.."
string(REGEX REPLACE
    "\"directory\": \"[^\"]*\""
    "\"directory\": \"${SOURCE_DIR}\""
    _db_content
    "${_db_content}"
)

# Write the fixed content back
file(WRITE "${INPUT_FILE}" "${_db_content}")

message(STATUS "Fixed compilation database directory field: ${INPUT_FILE}")
