# =============================================================================
# DetectLLVMPaths.cmake - LLVM Path Detection Utility
# =============================================================================
# This module detects LLVM include and library directories for building
# tools that link against LLVM/Clang libraries (e.g., the defer tool).
#
# On Unix (Linux/macOS):
#   Uses llvm-config to discover paths (the standard LLVM approach)
#
# On Windows:
#   Official LLVM pre-built binaries don't include llvm-config.exe,
#   so we detect paths directly from known installation locations:
#   - Scoop global: C:/ProgramData/scoop/apps/llvm/current
#   - Scoop user: %USERPROFILE%/scoop/apps/llvm/current
#   - Official installer/choco/winget: C:/Program Files/LLVM
#
# Outputs (variables set by this module):
#   LLVM_INCLUDE_DIRS  - LLVM include directory
#   LLVM_LIBRARY_DIRS  - LLVM library directory
#   LLVM_VERSION       - LLVM version string (e.g., "19.1.0")
#   LLVM_ROOT          - LLVM root directory (Windows only)
#
# Usage:
#   include(cmake/utils/DetectLLVMPaths.cmake)
#   # Then use LLVM_INCLUDE_DIRS, LLVM_LIBRARY_DIRS, LLVM_VERSION
#
# You can pre-set LLVM_INCLUDE_DIRS and LLVM_LIBRARY_DIRS to skip detection.
# =============================================================================

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_DETECT_LLVM_PATHS_INCLUDED)
    return()
endif()
set(_ASCIICHAT_DETECT_LLVM_PATHS_INCLUDED TRUE)

# Skip if paths already provided
if(LLVM_INCLUDE_DIRS AND LLVM_LIBRARY_DIRS)
    message(STATUS "LLVM paths provided: ${LLVM_INCLUDE_DIRS}, ${LLVM_LIBRARY_DIRS}")
    return()
endif()

# =============================================================================
# Windows: Direct path detection (no llvm-config available)
# =============================================================================
if(WIN32)
    # Search known LLVM installation locations
    # NOTE: We check for LLVMSupport.lib because the official Windows LLVM
    # installer only includes compiler binaries, NOT the development libraries
    # needed for building tools like the defer instrumentation pass.
    # Scoop/choco/winget all use the official installer, so they also lack these.
    # Full LLVM dev libraries are only available from:
    #   - vovkos/llvm-package-windows (GitHub)
    #   - Building LLVM from source
    set(_llvm_search_roots
        # Scoop global install (--global flag installs to ProgramData)
        "C:/ProgramData/scoop/apps/llvm/current"
        # Scoop user install
        "$ENV{USERPROFILE}/scoop/apps/llvm/current"
        # Official LLVM installer, choco, winget
        "C:/Program Files/LLVM"
        "$ENV{PROGRAMFILES}/LLVM"
        # Less common locations
        "C:/LLVM"
        "$ENV{LOCALAPPDATA}/Programs/LLVM"
    )

    foreach(_llvm_root IN LISTS _llvm_search_roots)
        # Check not just for include/lib dirs, but for actual LLVM development
        # libraries. The official Windows installer has include/lib but NO .lib files
        # for development (only import libs for clang.exe, not LLVMSupport.lib etc.)
        if(EXISTS "${_llvm_root}/include" AND EXISTS "${_llvm_root}/lib")
            # Verify this is a full LLVM installation with development libraries
            if(EXISTS "${_llvm_root}/lib/LLVMSupport.lib" OR
               EXISTS "${_llvm_root}/lib/libLLVMSupport.a")
                set(LLVM_ROOT "${_llvm_root}")
                set(LLVM_INCLUDE_DIRS "${_llvm_root}/include")
                set(LLVM_LIBRARY_DIRS "${_llvm_root}/lib")
                message(STATUS "Found LLVM installation with dev libraries at: ${_llvm_root}")
                break()
            else()
                message(STATUS "Skipping ${_llvm_root} - has binaries but no dev libraries (LLVMSupport.lib missing)")
            endif()
        endif()
    endforeach()
    unset(_llvm_search_roots)

    if(NOT LLVM_INCLUDE_DIRS OR NOT LLVM_LIBRARY_DIRS)
        message(FATAL_ERROR "Could not find LLVM installation with development libraries on Windows.\n"
            "\n"
            "The defer() instrumentation tool requires LLVM development libraries\n"
            "(LLVMSupport.lib, clangTooling.lib, etc.) which are NOT included in:\n"
            "  - Official LLVM Windows installer (releases.llvm.org)\n"
            "  - scoop install llvm\n"
            "  - winget install LLVM.LLVM\n"
            "  - choco install llvm\n"
            "\n"
            "These packages only include the compiler binaries (clang.exe), not dev libs.\n"
            "\n"
            "Solutions:\n"
            "  1. Download LLVM with dev libraries from:\n"
            "     https://github.com/vovkos/llvm-package-windows/releases\n"
            "     Extract to C:/LLVM or C:/ProgramData/scoop/apps/llvm/current\n"
            "\n"
            "  2. Build LLVM from source with development libraries\n"
            "\n"
            "  3. Set paths manually:\n"
            "     -DLLVM_INCLUDE_DIRS=<path>/include -DLLVM_LIBRARY_DIRS=<path>/lib")
    endif()

    # Get version from LLVM headers
    if(EXISTS "${LLVM_INCLUDE_DIRS}/llvm/Config/llvm-config.h")
        file(STRINGS "${LLVM_INCLUDE_DIRS}/llvm/Config/llvm-config.h" _llvm_version_line
             REGEX "^#define LLVM_VERSION_STRING ")
        if(_llvm_version_line MATCHES "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
            set(LLVM_VERSION "${CMAKE_MATCH_1}")
        endif()
        unset(_llvm_version_line)
    endif()
    if(NOT LLVM_VERSION)
        set(LLVM_VERSION "unknown")
    endif()

# =============================================================================
# Unix (Linux/macOS): Use llvm-config
# =============================================================================
else()
    # Check if llvm-config was passed from parent project (e.g., Defer.cmake)
    if(LLVM_CONFIG_EXECUTABLE AND EXISTS "${LLVM_CONFIG_EXECUTABLE}")
        set(_LLVM_CONFIG_EXECUTABLE "${LLVM_CONFIG_EXECUTABLE}")
    else()
        # Build list of llvm-config names (versioned variants)
        set(_llvm_config_names llvm-config)
        foreach(_ver IN ITEMS 21 20 19 18 17 16 15)
            list(APPEND _llvm_config_names llvm-config-${_ver})
        endforeach()

        # Search paths for llvm-config
        if(APPLE)
            set(_llvm_config_hints
                /opt/homebrew/opt/llvm/bin    # Homebrew on Apple Silicon
                /usr/local/opt/llvm/bin        # Homebrew on Intel Mac
                /opt/homebrew/bin
                /usr/local/bin
                /usr/bin
            )
        else()
            # Linux
            set(_llvm_config_hints
                /usr/lib/llvm-21/bin
                /usr/lib/llvm-20/bin
                /usr/lib/llvm-19/bin
                /usr/lib/llvm-18/bin
                /usr/lib/llvm-17/bin
                /usr/lib/llvm-16/bin
                /usr/lib/llvm-15/bin
                /usr/local/bin
                /usr/bin
            )
        endif()

        find_program(_LLVM_CONFIG_EXECUTABLE
            NAMES ${_llvm_config_names}
            HINTS ${_llvm_config_hints}
            DOC "Path to llvm-config"
        )
        unset(_llvm_config_names)
        unset(_llvm_config_hints)
    endif()

    if(NOT _LLVM_CONFIG_EXECUTABLE)
        message(FATAL_ERROR "llvm-config not found. Install LLVM:\n"
            "  macOS:   brew install llvm\n"
            "  Linux:   sudo apt install llvm  OR  sudo pacman -S llvm\n"
            "Or set LLVM_INCLUDE_DIRS and LLVM_LIBRARY_DIRS manually.")
    endif()

    # Query llvm-config for paths
    execute_process(COMMAND "${_LLVM_CONFIG_EXECUTABLE}" --includedir
        OUTPUT_VARIABLE LLVM_INCLUDE_DIRS OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(COMMAND "${_LLVM_CONFIG_EXECUTABLE}" --libdir
        OUTPUT_VARIABLE LLVM_LIBRARY_DIRS OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(COMMAND "${_LLVM_CONFIG_EXECUTABLE}" --version
        OUTPUT_VARIABLE LLVM_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(COMMAND "${_LLVM_CONFIG_EXECUTABLE}" --prefix
        OUTPUT_VARIABLE LLVM_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    # Store the llvm-config path for reference
    set(LLVM_CONFIG_EXECUTABLE "${_LLVM_CONFIG_EXECUTABLE}" CACHE FILEPATH "llvm-config executable")
    unset(_LLVM_CONFIG_EXECUTABLE CACHE)
endif()

# =============================================================================
# Status output
# =============================================================================
message(STATUS "LLVM version: ${LLVM_VERSION}")
message(STATUS "LLVM include: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM lib: ${LLVM_LIBRARY_DIRS}")
