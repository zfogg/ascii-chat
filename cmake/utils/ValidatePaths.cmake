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
# Note: Patterns must be specific enough to avoid false positives from URLs
set(PATH_PATTERNS
    # Windows user directories (forward and back slash variants)
    "C:\\\\Users\\\\"
    "C:/Users/"
    # Unix home directories
    "/home/"
    # macOS user directories
    "/Users/"
    # WSL paths
    "/mnt/c/Users/"
    "/mnt/d/"
)

# Add any extra patterns
if(EXTRA_PATTERNS)
    list(APPEND PATH_PATTERNS ${EXTRA_PATTERNS})
endif()

# Track found paths (as a newline-separated string to avoid semicolon issues)
set(FOUND_PATHS_TEXT "")
set(NUM_FOUND 0)

# Process line by line using string(FIND) on newlines
string(LENGTH "${STRINGS_OUTPUT}" OUTPUT_LEN)
set(POS 0)
set(LINE_START 0)

while(POS LESS OUTPUT_LEN AND NUM_FOUND LESS 20)
    # Get remaining output from current position
    string(SUBSTRING "${STRINGS_OUTPUT}" ${LINE_START} -1 REMAINING_OUTPUT)

    # Find next newline in remaining output
    string(FIND "${REMAINING_OUTPUT}" "\n" NEWLINE_POS)

    if(NEWLINE_POS EQUAL -1)
        # Last line (no trailing newline)
        set(LINE "${REMAINING_OUTPUT}")
        set(POS ${OUTPUT_LEN})
    else()
        # Extract line (NEWLINE_POS is relative to REMAINING_OUTPUT)
        if(NEWLINE_POS GREATER 0)
            string(SUBSTRING "${REMAINING_OUTPUT}" 0 ${NEWLINE_POS} LINE)
        else()
            set(LINE "")
        endif()
        # Move past the newline
        math(EXPR LINE_START "${LINE_START} + ${NEWLINE_POS} + 1")
        set(POS ${LINE_START})
    endif()

    # Skip empty lines
    if(NOT LINE OR LINE STREQUAL "")
        continue()
    endif()

    # Check each pattern
    foreach(PATTERN IN LISTS PATH_PATTERNS)
        string(FIND "${LINE}" "${PATTERN}" MATCH_POS)
        if(NOT MATCH_POS EQUAL -1)
            # Found a match
            math(EXPR NUM_FOUND "${NUM_FOUND} + 1")
            # Truncate long lines for readability
            string(LENGTH "${LINE}" LINE_LEN)
            if(LINE_LEN GREATER 100)
                string(SUBSTRING "${LINE}" 0 100 LINE)
                set(LINE "${LINE}...")
            endif()
            set(FOUND_PATHS_TEXT "${FOUND_PATHS_TEXT}    ${LINE}\n")
            break()
        endif()
    endforeach()
endwhile()

# Add truncation notice if we hit the limit
if(NUM_FOUND EQUAL 20)
    set(FOUND_PATHS_TEXT "${FOUND_PATHS_TEXT}    ... (truncated, more paths may exist)\n")
endif()

# Report results
if(NUM_FOUND GREATER 0)
    message(FATAL_ERROR
        "=============================================================================\n"
        "  RELEASE BUILD VALIDATION FAILED: Developer paths found in binary!\n"
        "=============================================================================\n"
        "  Binary: ${BINARY}\n"
        "\n"
        "  Found ${NUM_FOUND} strings containing developer/build paths:\n"
        "\n"
        "${FOUND_PATHS_TEXT}"
        "\n"
        "  This is a security/privacy concern - release binaries should not contain\n"
        "  paths from the build environment.\n"
        "\n"
        "  Possible causes:\n"
        "    - __FILE__ macros in logging/assertions\n"
        "    - Debug info not fully stripped\n"
        "    - Static library paths embedded by linker\n"
        "\n"
        "  Solutions:\n"
        "    - Use -ffile-prefix-map to remap source paths at compile time\n"
        "    - Ensure debug info is stripped (strip --strip-all)\n"
        "    - Check for __FILE__ usage in release builds\n"
        "    - Use relative paths for static libraries\n"
        "=============================================================================\n"
    )
endif()

message(STATUS "Path validation passed: no developer paths found in ${BINARY}")
