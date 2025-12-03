# =============================================================================
# Path Conversion Utility
# =============================================================================
# Provides functions for converting paths between different formats,
# particularly for use with shell scripts on Windows (Git Bash, WSL).
#
# Usage:
#   convert_path_for_shell(<input_path> <output_var>)
#
# Converts Windows paths to Unix-style paths for shell script compatibility:
#   - C:/path/to/file -> /mnt/c/path/to/file (WSL format)
#   - Unix paths pass through unchanged
#
# Example:
#   convert_path_for_shell("${CMAKE_BINARY_DIR}" _shell_path)
#   # Windows: C:/Users/me/build -> /mnt/c/Users/me/build
#   # Unix: /home/me/build -> /home/me/build (unchanged)
# =============================================================================

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_PATH_CONVERSION_INCLUDED)
    return()
endif()
set(_ASCIICHAT_PATH_CONVERSION_INCLUDED TRUE)

# Convert a path to shell-compatible format
# On Windows with WSL, converts C:/path to /mnt/c/path
# On Unix/Git Bash, paths pass through unchanged
function(convert_path_for_shell input_path output_var)
    if(NOT input_path)
        set(${output_var} "" PARENT_SCOPE)
        return()
    endif()

    # Convert to CMake path format (forward slashes)
    cmake_path(CONVERT "${input_path}" TO_CMAKE_PATH_LIST _posix_path)

    # Check if this is a Windows drive letter path (e.g., C:/...)
    if(_posix_path MATCHES "^([A-Za-z]):/(.*)")
        # Convert to WSL mount path format
        string(TOLOWER "${CMAKE_MATCH_1}" _drive_letter)
        set(_result "/mnt/${_drive_letter}/${CMAKE_MATCH_2}")
    else()
        # Unix path or already converted - use as-is
        set(_result "${_posix_path}")
    endif()

    set(${output_var} "${_result}" PARENT_SCOPE)
endfunction()

# Check if bash executable is WSL (System32\bash.exe)
# Returns TRUE if the bash is WSL, FALSE otherwise
function(is_wsl_bash bash_executable output_var)
    if(WIN32 AND bash_executable MATCHES ".*[Ss]ystem32[/\\\\]bash\\.exe$")
        set(${output_var} TRUE PARENT_SCOPE)
    else()
        set(${output_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

