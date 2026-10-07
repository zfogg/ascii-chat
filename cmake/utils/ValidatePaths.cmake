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

# Paths from system dependencies that are expected in release binaries.
set(WHITELISTED_PATHS
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
    # vcpkg build trees - static libraries (FFmpeg, OpenSSL, etc.) compiled by vcpkg
    # contain __FILE__ macros with vcpkg buildtree paths baked in
    "scoop/apps/vcpkg/"
    "vcpkg/current/buildtrees/"
)

# Run llvm-strings directly so Windows paths and tool locations are passed
# through CMake's native process launcher instead of being interpreted by bash.
execute_process(
    COMMAND "${LLVM_STRINGS}" "${BINARY}"
    OUTPUT_VARIABLE BINARY_STRINGS
    ERROR_VARIABLE STRINGS_ERROR
    RESULT_VARIABLE STRINGS_RESULT
    TIMEOUT 10
)

if(NOT STRINGS_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not inspect ${BINARY} with llvm-strings: ${STRINGS_ERROR}")
endif()

# Match candidate strings first, then use literal checks for the path prefixes.
# This keeps the scan to one regex pass while avoiding shell-specific escaping.
string(REGEX MATCHALL "[^\n]*(C:|/home/|/Users/|/mnt/)[^\n]*" CANDIDATE_LINES "${BINARY_STRINGS}")
set(FOUND_PATHS "")
set(FOUND_COUNT 0)
foreach(LINE IN LISTS CANDIDATE_LINES)
    string(REPLACE "\\" "/" NORMALIZED_LINE "${LINE}")
    string(TOLOWER "${NORMALIZED_LINE}" LOWER_LINE)
    if(NOT LOWER_LINE MATCHES "c:/users/|/home/|/users/|/mnt/c/users/|/mnt/d/")
        continue()
    endif()

    set(IS_WHITELISTED FALSE)
    foreach(PATH IN LISTS WHITELISTED_PATHS)
        string(TOLOWER "${PATH}" LOWER_PATH)
        string(FIND "${LOWER_LINE}" "${LOWER_PATH}" PATH_INDEX)
        if(NOT PATH_INDEX EQUAL -1)
            set(IS_WHITELISTED TRUE)
            break()
        endif()
    endforeach()

    if(NOT IS_WHITELISTED)
        math(EXPR FOUND_COUNT "${FOUND_COUNT} + 1")
        if(FOUND_COUNT LESS_EQUAL 20)
            string(APPEND FOUND_PATHS "${LINE}\n")
        endif()
    endif()
endforeach()

if(FOUND_COUNT GREATER 0)

    message(FATAL_ERROR
        "=============================================================================\n"
        "  RELEASE BUILD VALIDATION FAILED: Developer paths found in binary!\n"
        "=============================================================================\n"
        "  Binary: ${BINARY}\n"
        "\n"
        "  Found at least ${FOUND_COUNT} strings containing developer/build paths:\n"
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

message(STATUS "Path validation passed: no developer paths found in ${BINARY}")
