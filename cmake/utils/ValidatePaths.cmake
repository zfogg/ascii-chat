# =============================================================================
# ValidatePaths.cmake - Check for developer paths in Release binaries
# =============================================================================
# This script is run post-build to validate that Release binaries don't contain
# embedded developer paths like C:\Users\*, /home/*, etc.
#
# These paths can leak information about the build environment and should not
# appear in release binaries. The build will fail if such paths are found.
#
# Required variables (passed via -D):
#   LLVM_STRINGS - Path to llvm-strings executable
#   BINARY - Path to the binary to check
#
# Optional variables:
#   EXTRA_PATTERNS - Additional patterns to check (semicolon-separated)
# =============================================================================

if(NOT LLVM_STRINGS)
    message(FATAL_ERROR "LLVM_STRINGS not specified")
endif()

if(NOT BINARY)
    message(FATAL_ERROR "BINARY not specified")
endif()

if(NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "Binary not found: ${BINARY}")
endif()

# Run llvm-strings on the binary
execute_process(
    COMMAND "${LLVM_STRINGS}" "${BINARY}"
    OUTPUT_VARIABLE STRINGS_OUTPUT
    ERROR_VARIABLE STRINGS_ERROR
    RESULT_VARIABLE STRINGS_RESULT
)

if(NOT STRINGS_RESULT EQUAL 0)
    message(FATAL_ERROR "llvm-strings failed: ${STRINGS_ERROR}")
endif()

# Define patterns to check for developer paths
# These patterns indicate build environment leakage
set(PATH_PATTERNS
    # Windows user directories
    "C:\\\\Users\\\\"
    "C:/Users/"
    # Unix home directories
    "/home/"
    "/Users/"
    # Common build directories that shouldn't appear
    "/src/"
    "/build/"
    # WSL paths
    "/mnt/c/Users/"
    "/mnt/d/"
)

# Add any extra patterns
if(EXTRA_PATTERNS)
    list(APPEND PATH_PATTERNS ${EXTRA_PATTERNS})
endif()

# Convert output to a list of lines for checking
string(REPLACE "\n" ";" STRINGS_LINES "${STRINGS_OUTPUT}")

# Track found paths
set(FOUND_PATHS "")

foreach(LINE IN LISTS STRINGS_LINES)
    # Skip empty lines
    if(NOT LINE)
        continue()
    endif()

    foreach(PATTERN IN LISTS PATH_PATTERNS)
        # Check if line contains the pattern
        string(FIND "${LINE}" "${PATTERN}" MATCH_POS)
        if(NOT MATCH_POS EQUAL -1)
            # Found a match - record it (but limit to avoid huge output)
            list(LENGTH FOUND_PATHS NUM_FOUND)
            if(NUM_FOUND LESS 20)
                list(APPEND FOUND_PATHS "${LINE}")
            elseif(NUM_FOUND EQUAL 20)
                list(APPEND FOUND_PATHS "... (truncated, more paths found)")
            endif()
            break()
        endif()
    endforeach()
endforeach()

# Report results
list(LENGTH FOUND_PATHS NUM_FOUND)
if(NUM_FOUND GREATER 0)
    message(FATAL_ERROR
        "=============================================================================\n"
        "  RELEASE BUILD VALIDATION FAILED: Developer paths found in binary!\n"
        "=============================================================================\n"
        "  Binary: ${BINARY}\n"
        "\n"
        "  The following developer paths were found embedded in the release binary:\n"
        "${FOUND_PATHS}\n"
        "\n"
        "  This is a security/privacy concern - release binaries should not contain\n"
        "  paths from the build environment.\n"
        "\n"
        "  Possible causes:\n"
        "    - __FILE__ macros in logging/assertions\n"
        "    - Debug info not fully stripped\n"
        "    - Compiler embedding source paths\n"
        "\n"
        "  Solutions:\n"
        "    - Use -ffile-prefix-map to remap paths at compile time\n"
        "    - Ensure debug info is stripped (strip --strip-all)\n"
        "    - Check for __FILE__ usage in release builds\n"
        "=============================================================================\n"
    )
endif()

message(STATUS "Path validation passed: no developer paths found in ${BINARY}")
