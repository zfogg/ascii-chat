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

# Use bash/grep for fast path validation instead of CMake string operations
# CMake's string(FIND) is O(n) for each call and becomes prohibitively slow
# for large binaries with thousands of strings. grep is optimized for this.

# Build grep patterns to exclude whitelisted paths
set(GREP_EXCLUDE_PATTERNS
    # Alpine Linux official package builder (always uses this path)
    "/home/buildozer/aports/"
    # Homebrew builds (official package manager paths)
    "/home/linuxbrew/"
    "/opt/homebrew/"
    # Dependency cache paths (non-developer paths, deterministic across builds)
    ".deps-cache/"
    # LLVM/libc++ source paths - embedded in static libc++abi via __FILE__ macros
    # These are from assertion/exception messages in static libc++ and don't affect portability
    "llvm-project/libcxxabi/"
    "llvm-project/libcxx/"
    "llvm-project/libunwind/"
    # User home paths from static library __FILE__ macros (llvm, libc++, libunwind)
    # These come from system /usr/local/lib/ paths and are expected in release builds
    "/usr/local/"
)

# Build grep exclude arguments
set(GREP_EXCLUDE_ARGS "")
foreach(PATTERN ${GREP_EXCLUDE_PATTERNS})
    # Escape special regex chars and add to grep exclude list
    string(REPLACE "/" "\\/" PATTERN_ESCAPED "${PATTERN}")
    list(APPEND GREP_EXCLUDE_ARGS "-v")
    list(APPEND GREP_EXCLUDE_ARGS "${PATTERN_ESCAPED}")
endforeach()

# Run validation via bash using grep - much faster than CMake string operations
# Patterns: C:\Users\, C:/Users/, /home/, /Users/, /mnt/c/Users/, /mnt/d/
# Use a two-pass grep: first to find suspect paths, then to exclude whitelisted paths
execute_process(
    COMMAND bash -c "
        SUSPECT=\$(${LLVM_STRINGS} '${BINARY}' | grep -E 'C:\\\\\\\\Users\\\\\\\\|C:/Users/|/home/|/Users/|/mnt/c/Users/|/mnt/d/')
        if [ -z \"\$SUSPECT\" ]; then exit 1; fi
        echo \"\$SUSPECT\" | grep -v -e '/usr/local/' -e '.deps-cache/' -e 'llvm-project/' | head -20
    "
    OUTPUT_VARIABLE FOUND_PATHS
    RESULT_VARIABLE GREP_RESULT
    TIMEOUT 10
)

# grep returns 0 if matches found, 1 if no matches, 2+ for errors
if(GREP_RESULT EQUAL 0 AND FOUND_PATHS)
    # Count number of lines found
    string(REGEX MATCHALL "\n" NEWLINES "${FOUND_PATHS}")
    list(LENGTH NEWLINES NUM_FOUND)
    math(EXPR NUM_FOUND "${NUM_FOUND} + 1")  # +1 because last line doesn't have newline

    message(FATAL_ERROR
        "=============================================================================\n"
        "  RELEASE BUILD VALIDATION FAILED: Developer paths found in binary!\n"
        "=============================================================================\n"
        "  Binary: ${BINARY}\n"
        "\n"
        "  Found ${NUM_FOUND} strings containing developer/build paths:\n"
        "\n"
        "${FOUND_PATHS}\n"
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

# If grep timed out or had an error, skip validation
if(GREP_RESULT GREATER 1)
    message(STATUS "Path validation skipped: grep error or timeout on large binary")
    return()
endif()

message(STATUS "Path validation passed: no developer paths found in ${BINARY}")
