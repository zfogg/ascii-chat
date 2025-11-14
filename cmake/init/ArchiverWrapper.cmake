# =============================================================================
# Archiver Wrapper Configuration
# =============================================================================
# This module configures CMAKE_AR to use a wrapper script that renames object
# files before archiving to avoid conflicts when combining static libraries.
#
# IMPORTANT: This MUST be included BEFORE any static libraries are created
# (i.e., before Libraries.cmake) so that CMAKE_AR is set correctly.
#
# Outputs:
#   - Sets CMAKE_AR to wrapper script (ar_wrapper.cmd on Windows, ar_wrapper.sh on POSIX)
#   - Creates wrapper scripts in ${CMAKE_BINARY_DIR}/cmake/scripts/
# =============================================================================

# Only configure the wrapper for Release builds where we build STATIC libraries
# Debug/Dev/Coverage builds on Windows use OBJECT libraries, so they don't need this
if(WIN32 AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev" OR CMAKE_BUILD_TYPE STREQUAL "Coverage"))
    # Skip ar wrapper configuration for Windows Debug/Dev/Coverage builds
    # These use OBJECT libraries, not STATIC libraries
    return()
endif()

# Save the real archiver path before overriding CMAKE_AR
set(REAL_AR "${CMAKE_AR}" CACHE STRING "Real archiver path" FORCE)

# Configure the wrapper script from template
set(AR_WRAPPER_SH "${CMAKE_BINARY_DIR}/cmake/scripts/ar_wrapper.sh")
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/scripts/ar_wrapper.sh.in"
    "${AR_WRAPPER_SH}"
    @ONLY
    NEWLINE_STYLE UNIX
)
execute_process(COMMAND chmod +x "${AR_WRAPPER_SH}" ERROR_QUIET)

if (WIN32)
    # Build hint paths for finding bash on Windows
    # Note: We must read ProgramFiles(x86) separately because CMake can't parse parens in $ENV{}
    set(_pf "$ENV{ProgramFiles}")
    set(_pf_x86 "$ENV{ProgramFiles\(x86\)}")
    set(_localappdata "$ENV{LOCALAPPDATA}")

    set(_asciichat_bash_hint_paths
        "${_pf}/Git/usr/bin"
        "${_pf}/Git/bin"
        "${_pf_x86}/Git/usr/bin"
        "${_pf_x86}/Git/bin"
        "${_localappdata}/Programs/Git/usr/bin"
        "${_localappdata}/Programs/Git/bin"
    )
    find_program(
        ASCIICHAT_BASH_EXECUTABLE
        NAMES bash.exe bash sh.exe sh
        HINTS ${_asciichat_bash_hint_paths}
    )
    if (NOT ASCIICHAT_BASH_EXECUTABLE)
        message(FATAL_ERROR "ascii-chat build requires a POSIX shell (bash) to wrap the archiver on Windows. Install Git for Windows or another bash distribution and ensure it is in PATH.")
    endif()

    file(TO_CMAKE_PATH "${AR_WRAPPER_SH}" AR_WRAPPER_SH_FOR_BASH)
    set(AR_WRAPPER_CMD "${CMAKE_BINARY_DIR}/cmake/scripts/ar_wrapper.cmd")
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/scripts/ar_wrapper.cmd.in"
        "${AR_WRAPPER_CMD}"
        @ONLY
        NEWLINE_STYLE DOS
    )
    set(CMAKE_AR "${AR_WRAPPER_CMD}" CACHE STRING "Archiver with object renaming" FORCE)
    message(STATUS "${Yellow}Build:${ColorReset} Using archiver wrapper: ${BoldBlue}${AR_WRAPPER_CMD}${ColorReset} -> ${BoldBlue}${REAL_AR}${ColorReset}")
else()
    # Override CMAKE_AR to use our wrapper directly on POSIX platforms
    set(CMAKE_AR "${AR_WRAPPER_SH}" CACHE STRING "Archiver with object renaming" FORCE)
    message(STATUS "${Yellow}Build:${ColorReset} Using archiver wrapper: ${BoldBlue}${AR_WRAPPER_SH}${ColorReset} -> ${BoldBlue}${REAL_AR}${ColorReset}")
endif()
