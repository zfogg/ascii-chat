# =============================================================================
# FindPrograms.cmake - Centralized Program Discovery
# =============================================================================
# This module finds all external programs used by the build system ONCE.
# Other modules should use the cached variables instead of calling find_program.
#
# Naming convention: ASCIICHAT_<NAME>_EXECUTABLE
#
# Programs found:
#   - ASCIICHAT_CCACHE_EXECUTABLE: ccache compiler cache
#   - ASCIICHAT_LLVM_CONFIG_EXECUTABLE: llvm-config for LLVM toolchain info
#   - ASCIICHAT_CLANG_EXECUTABLE: clang C compiler
#   - ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE: clang++ C++ compiler
#   - ASCIICHAT_CLANG_TIDY_EXECUTABLE: clang-tidy static analyzer
#   - ASCIICHAT_CLANG_FORMAT_EXECUTABLE: clang-format code formatter
#   - ASCIICHAT_CPPCHECK_EXECUTABLE: cppcheck static analyzer
#   - ASCIICHAT_BASH_EXECUTABLE: bash shell (Git Bash on Windows)
#   - ASCIICHAT_STRIP_EXECUTABLE: strip for removing symbols
#   - ASCIICHAT_OBJCOPY_EXECUTABLE: objcopy for binary manipulation
#   - ASCIICHAT_PYTHON3_EXECUTABLE: Python 3 interpreter
#   - ASCIICHAT_NINJA_EXECUTABLE: Ninja build system
#   - ASCIICHAT_SCAN_BUILD_EXECUTABLE: scan-build static analyzer wrapper
#   - ASCIICHAT_ZIP_EXECUTABLE: zip archiver
#   - ASCIICHAT_LLVM_AR_EXECUTABLE: llvm-ar archiver
#   - ASCIICHAT_LLVM_RANLIB_EXECUTABLE: llvm-ranlib archive indexer
#   - ASCIICHAT_LLVM_NM_EXECUTABLE: llvm-nm symbol table viewer
#   - ASCIICHAT_LLVM_READELF_EXECUTABLE: llvm-readelf for ELF binary analysis
#   - ASCIICHAT_LLVM_OBJDUMP_EXECUTABLE: llvm-objdump for binary disassembly/analysis
#   - ASCIICHAT_LLVM_STRIP_EXECUTABLE: llvm-strip for removing symbols
#   - ASCIICHAT_LLD_EXECUTABLE: LLD linker
#   - ASCIICHAT_GMAKE_EXECUTABLE: GNU Make (macOS)
#   - ASCIICHAT_CLANG_CL_EXECUTABLE: clang-cl MSVC-compatible compiler (Windows only)
#   - ASCIICHAT_LLVM_LIB_EXECUTABLE: llvm-lib library archive tool (Windows only)
#   - ASCIICHAT_MUSL_GCC_EXECUTABLE: musl-gcc compiler wrapper (Linux only, for USE_MUSL builds)
#   - ASCIICHAT_GCC_EXECUTABLE: GCC compiler (Linux only, used by musl-gcc via REALGCC)
#
# Exported paths (for modules that need to find additional LLVM tools):
#   - ASCIICHAT_LLVM_BINDIR: LLVM bin directory from llvm-config --bindir
#   - ASCIICHAT_LLVM_TOOL_SEARCH_PATHS: Search paths for LLVM tools (bindir first)
#   - ASCIICHAT_LLVM_SEARCH_PATHS: Fallback LLVM search paths
#
# Prerequisites:
#   - Colors.cmake must be included first (for status messages)
#   - Can run before or after project() - uses CMAKE_HOST_SYSTEM_NAME
#
# Usage in other modules:
#   if(ASCIICHAT_CCACHE_EXECUTABLE)
#       set(CMAKE_C_COMPILER_LAUNCHER "${ASCIICHAT_CCACHE_EXECUTABLE}")
#   endif()
#
#   # Finding additional LLVM tools:
#   find_program(MY_TOOL my-tool HINTS ${ASCIICHAT_LLVM_TOOL_SEARCH_PATHS})
# =============================================================================

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_FIND_PROGRAMS_INCLUDED)
    return()
endif()
set(_ASCIICHAT_FIND_PROGRAMS_INCLUDED TRUE)

# =============================================================================
# Platform-specific search paths
# =============================================================================
if(WIN32)
    set(_LLVM_SEARCH_PATHS
        "$ENV{PROGRAMFILES}/LLVM/bin"
        "$ENV{LOCALAPPDATA}/Programs/LLVM/bin"
        "C:/Program Files/LLVM/bin"
        "$ENV{USERPROFILE}/scoop/apps/llvm/current/bin"
        "$ENV{USERPROFILE}/scoop/shims"
    )
    set(_BASH_SEARCH_PATHS
        # Git for Windows (64-bit)
        "C:/Program Files/Git/usr/bin"
        "C:/Program Files/Git/bin"
        "C:/Program Files/Git/cmd"
        "$ENV{ProgramFiles}/Git/usr/bin"
        "$ENV{ProgramFiles}/Git/bin"
        "$ENV{ProgramFiles}/Git/cmd"
        "$ENV{ProgramW6432}/Git/usr/bin"
        "$ENV{ProgramW6432}/Git/bin"
        "$ENV{ProgramW6432}/Git/cmd"
        # Git for Windows (user install)
        "$ENV{LOCALAPPDATA}/Programs/Git/usr/bin"
        "$ENV{LOCALAPPDATA}/Programs/Git/bin"
        "$ENV{LOCALAPPDATA}/Programs/Git/cmd"
        # WSL bash (last resort)
        "$ENV{SystemRoot}/system32"
    )
elseif(APPLE)
    set(_LLVM_SEARCH_PATHS
        /usr/local/bin
        /usr/local/opt/llvm/bin
        /opt/homebrew/opt/llvm/bin
        /opt/homebrew/bin
        /usr/bin
    )
    set(_BASH_SEARCH_PATHS /bin /usr/bin /usr/local/bin)
else()
    # Linux
    set(_LLVM_SEARCH_PATHS
        /usr/local/bin
        /usr/lib/llvm/bin
        /usr/lib/llvm-21/bin
        /usr/lib/llvm-20/bin
        /usr/lib/llvm-19/bin
        /usr/lib/llvm-18/bin
        /usr/lib/llvm-17/bin
        /usr/lib/llvm-16/bin
        /usr/lib/llvm-15/bin
        /usr/bin
    )
    set(_BASH_SEARCH_PATHS /bin /usr/bin)
endif()

# Export LLVM search paths for use by other modules (e.g., LLVM.cmake)
set(ASCIICHAT_LLVM_SEARCH_PATHS "${_LLVM_SEARCH_PATHS}" CACHE INTERNAL "LLVM search paths")
unset(_LLVM_SEARCH_PATHS)

# =============================================================================
# ccache - Compiler cache for faster rebuilds
# =============================================================================
if(WIN32)
    set(_CCACHE_HINTS
        "$ENV{USERPROFILE}/scoop/shims"
        "$ENV{LOCALAPPDATA}/Programs/ccache"
    )
elseif(APPLE)
    set(_CCACHE_HINTS
        /opt/homebrew/bin
        /usr/local/bin
    )
else()
    set(_CCACHE_HINTS /usr/lib/ccache /usr/local/bin)
endif()
find_program(ASCIICHAT_CCACHE_EXECUTABLE ccache
    HINTS ${_CCACHE_HINTS}
    DOC "ccache compiler cache"
)
unset(_CCACHE_HINTS)

# =============================================================================
# LLVM Version Configuration
# =============================================================================
# Supported LLVM versions (newest first for priority)
set(_LLVM_SUPPORTED_VERSIONS 21 20 19 18)

# =============================================================================
# llvm-config - LLVM toolchain configuration (MUST BE FIRST)
# =============================================================================
# llvm-config is the SINGLE SOURCE OF TRUTH for which LLVM installation to use.
# All other LLVM tools (clang, clang++, llvm-ar, etc.) are discovered from
# the same installation via `llvm-config --bindir`.
#
# To use a specific LLVM installation, set ASCIICHAT_LLVM_CONFIG_EXECUTABLE:
#   cmake -B build -DASCIICHAT_LLVM_CONFIG_EXECUTABLE=/usr/bin/llvm-config

# Build versioned names list for llvm-config
set(_llvm_config_names llvm-config llvm-config.exe)
foreach(_ver IN LISTS _LLVM_SUPPORTED_VERSIONS)
    list(APPEND _llvm_config_names llvm-config-${_ver})
endforeach()

find_program(ASCIICHAT_LLVM_CONFIG_EXECUTABLE
    NAMES ${_llvm_config_names}
    PATHS ${ASCIICHAT_LLVM_SEARCH_PATHS}
    DOC "llvm-config for LLVM toolchain info (determines which LLVM installation to use)"
)
unset(_llvm_config_names)

# Get the LLVM bindir from llvm-config - ALL LLVM tools MUST come from this directory
# This ensures clang, clang++, clang-tidy, clang-format, llvm-ar, llvm-ranlib, LLD
# all come from the same LLVM installation for ABI compatibility.
set(_llvm_bindir_result "")
if(ASCIICHAT_LLVM_CONFIG_EXECUTABLE)
    execute_process(
        COMMAND "${ASCIICHAT_LLVM_CONFIG_EXECUTABLE}" --bindir
        OUTPUT_VARIABLE _llvm_bindir_result
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _llvm_config_exit_code
    )
    if(NOT _llvm_config_exit_code EQUAL 0)
        set(_llvm_bindir_result "")
        message(WARNING "Failed to get LLVM bindir from llvm-config")
    endif()
    unset(_llvm_config_exit_code)
endif()
# Cache it for use by other modules (use FORCE to update on reconfigure)
set(ASCIICHAT_LLVM_BINDIR "${_llvm_bindir_result}" CACHE INTERNAL "LLVM bin directory from llvm-config" FORCE)
unset(_llvm_bindir_result)

# Build unified LLVM tool search paths for fallback searches
if(ASCIICHAT_LLVM_BINDIR)
    set(ASCIICHAT_LLVM_TOOL_SEARCH_PATHS "${ASCIICHAT_LLVM_BINDIR}" ${ASCIICHAT_LLVM_SEARCH_PATHS})
else()
    set(ASCIICHAT_LLVM_TOOL_SEARCH_PATHS ${ASCIICHAT_LLVM_SEARCH_PATHS})
endif()
set(ASCIICHAT_LLVM_TOOL_SEARCH_PATHS "${ASCIICHAT_LLVM_TOOL_SEARCH_PATHS}" CACHE INTERNAL "LLVM tool search paths" FORCE)

# =============================================================================
# Helper macro to find LLVM tools from the correct installation
# =============================================================================
# When ASCIICHAT_LLVM_BINDIR is set, we ONLY search there (NO_DEFAULT_PATH)
# to ensure all tools come from the same LLVM installation.
# This prevents ABI mismatches when multiple LLVM versions are installed.
#
# Usage: _find_llvm_tool(VAR_NAME base_name [base_name2 ...])
# Example: _find_llvm_tool(ASCIICHAT_CLANG_EXECUTABLE clang)
#   -> searches for: clang, clang-21, clang-20, clang-19, clang-18
macro(_find_llvm_tool VAR_NAME)
    # Build list of names: base names first, then versioned variants
    set(_tool_names ${ARGN})
    foreach(_base IN ITEMS ${ARGN})
        foreach(_ver IN LISTS _LLVM_SUPPORTED_VERSIONS)
            list(APPEND _tool_names ${_base}-${_ver})
        endforeach()
    endforeach()

    if(ASCIICHAT_LLVM_BINDIR)
        # Strict mode: ONLY search in the llvm-config bindir
        find_program(${VAR_NAME}
            NAMES ${_tool_names}
            PATHS "${ASCIICHAT_LLVM_BINDIR}"
            NO_DEFAULT_PATH
            DOC "LLVM tool from ${ASCIICHAT_LLVM_BINDIR}"
        )
    else()
        # Fallback mode: search PATH and known locations
        find_program(${VAR_NAME}
            NAMES ${_tool_names}
            HINTS ${ASCIICHAT_LLVM_TOOL_SEARCH_PATHS}
            DOC "LLVM tool"
        )
    endif()
    unset(_tool_names)
endmacro()

# =============================================================================
# Clang compilers - clang, clang++
# =============================================================================
# These MUST come from the same LLVM installation as llvm-config
_find_llvm_tool(ASCIICHAT_CLANG_EXECUTABLE clang)
_find_llvm_tool(ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE clang++)

# =============================================================================
# Clang tools - clang-tidy, clang-format, scan-build
# =============================================================================
_find_llvm_tool(ASCIICHAT_CLANG_TIDY_EXECUTABLE clang-tidy)
_find_llvm_tool(ASCIICHAT_CLANG_FORMAT_EXECUTABLE clang-format)
_find_llvm_tool(ASCIICHAT_SCAN_BUILD_EXECUTABLE scan-build)

# =============================================================================
# cppcheck - Static analyzer
# =============================================================================
if(WIN32)
    set(_CPPCHECK_HINTS
        "$ENV{USERPROFILE}/scoop/shims"
        "$ENV{ProgramFiles}/Cppcheck"
        "C:/Program Files/Cppcheck"
    )
elseif(APPLE)
    set(_CPPCHECK_HINTS /opt/homebrew/bin /usr/local/bin)
else()
    set(_CPPCHECK_HINTS /usr/bin /usr/local/bin)
endif()
find_program(ASCIICHAT_CPPCHECK_EXECUTABLE
    NAMES cppcheck
    HINTS ${_CPPCHECK_HINTS}
    DOC "cppcheck static analyzer"
)
unset(_CPPCHECK_HINTS)

# =============================================================================
# bash - Shell (Git Bash on Windows)
# =============================================================================
# On Windows, prefer Git Bash for consistent path handling
find_program(ASCIICHAT_BASH_EXECUTABLE
    NAMES bash bash.exe
    HINTS ${_BASH_SEARCH_PATHS}
    NO_DEFAULT_PATH
    DOC "bash shell"
)
if(NOT ASCIICHAT_BASH_EXECUTABLE)
    find_program(ASCIICHAT_BASH_EXECUTABLE NAMES bash bash.exe DOC "bash shell")
endif()
# Windows fallback: try direct path checking for common Git Bash locations
if(NOT ASCIICHAT_BASH_EXECUTABLE AND WIN32)
    set(_bash_candidate_paths
        "$ENV{SystemRoot}/system32/bash.exe"
        "$ENV{ProgramFiles}/Git/usr/bin/bash.exe"
        "$ENV{ProgramFiles}/Git/bin/bash.exe"
        "$ENV{ProgramW6432}/Git/usr/bin/bash.exe"
        "$ENV{ProgramW6432}/Git/bin/bash.exe"
        "$ENV{LOCALAPPDATA}/Programs/Git/usr/bin/bash.exe"
        "$ENV{LOCALAPPDATA}/Programs/Git/bin/bash.exe"
        "C:/Program Files/Git/usr/bin/bash.exe"
        "C:/Program Files/Git/bin/bash.exe"
    )
    foreach(_bash_path IN LISTS _bash_candidate_paths)
        if(_bash_path AND EXISTS "${_bash_path}")
            set(ASCIICHAT_BASH_EXECUTABLE "${_bash_path}" CACHE FILEPATH "bash shell" FORCE)
            break()
        endif()
    endforeach()
    unset(_bash_candidate_paths)
endif()

# =============================================================================
# Binary manipulation tools - strip, objcopy
# =============================================================================
_find_llvm_tool(ASCIICHAT_STRIP_EXECUTABLE llvm-strip strip)
_find_llvm_tool(ASCIICHAT_OBJCOPY_EXECUTABLE llvm-objcopy objcopy)
_find_llvm_tool(ASCIICHAT_LLVM_STRIP_EXECUTABLE llvm-strip)

# =============================================================================
# LLVM archiver and analysis tools
# =============================================================================
_find_llvm_tool(ASCIICHAT_LLVM_AR_EXECUTABLE llvm-ar)
_find_llvm_tool(ASCIICHAT_LLVM_RANLIB_EXECUTABLE llvm-ranlib)
_find_llvm_tool(ASCIICHAT_LLVM_NM_EXECUTABLE llvm-nm)
_find_llvm_tool(ASCIICHAT_LLVM_READELF_EXECUTABLE llvm-readelf)
_find_llvm_tool(ASCIICHAT_LLVM_OBJDUMP_EXECUTABLE llvm-objdump)
_find_llvm_tool(ASCIICHAT_LLVM_STRINGS_EXECUTABLE llvm-strings)

# =============================================================================
# Windows-specific LLVM tools - clang-cl, llvm-lib
# =============================================================================
if(WIN32)
    _find_llvm_tool(ASCIICHAT_CLANG_CL_EXECUTABLE clang-cl)
    _find_llvm_tool(ASCIICHAT_LLVM_LIB_EXECUTABLE llvm-lib)
endif()

# =============================================================================
# Python 3 - For scripts
# =============================================================================
if(WIN32)
    set(_PYTHON_HINTS
        "$ENV{USERPROFILE}/scoop/shims"
        "$ENV{LOCALAPPDATA}/Programs/Python/Python312"
        "$ENV{LOCALAPPDATA}/Programs/Python/Python311"
        "$ENV{LOCALAPPDATA}/Programs/Python/Python310"
        "$ENV{ProgramFiles}/Python312"
        "$ENV{ProgramFiles}/Python311"
    )
elseif(APPLE)
    set(_PYTHON_HINTS /opt/homebrew/bin /usr/local/bin /usr/bin)
else()
    set(_PYTHON_HINTS /usr/bin /usr/local/bin)
endif()
find_program(ASCIICHAT_PYTHON3_EXECUTABLE
    NAMES python3 python
    HINTS ${_PYTHON_HINTS}
    DOC "Python 3 interpreter"
)
unset(_PYTHON_HINTS)

# =============================================================================
# Ninja - Build system
# =============================================================================
if(WIN32)
    set(_NINJA_HINTS
        "$ENV{USERPROFILE}/scoop/shims"
        "$ENV{LOCALAPPDATA}/Programs/Ninja"
        "$ENV{ProgramFiles}/Ninja"
    )
elseif(APPLE)
    set(_NINJA_HINTS /opt/homebrew/bin /usr/local/bin)
else()
    set(_NINJA_HINTS /usr/bin /usr/local/bin)
endif()
find_program(ASCIICHAT_NINJA_EXECUTABLE
    NAMES ninja ninja-build
    HINTS ${_NINJA_HINTS}
    DOC "Ninja build system"
)
unset(_NINJA_HINTS)

# =============================================================================
# zip - Archive creation
# =============================================================================
if(WIN32)
    set(_ZIP_HINTS
        "$ENV{USERPROFILE}/scoop/shims"
        "$ENV{ProgramFiles}/Git/usr/bin"
        "C:/Program Files/Git/usr/bin"
    )
elseif(APPLE)
    set(_ZIP_HINTS /usr/bin /opt/homebrew/bin)
else()
    set(_ZIP_HINTS /usr/bin /usr/local/bin)
endif()
find_program(ASCIICHAT_ZIP_EXECUTABLE
    NAMES zip
    HINTS ${_ZIP_HINTS}
    DOC "zip archiver"
)
unset(_ZIP_HINTS)

# =============================================================================
# Doxygen - Documentation generator
# =============================================================================
if(WIN32)
    set(_DOXYGEN_HINTS
        "$ENV{USERPROFILE}/scoop/shims"
        "$ENV{ProgramFiles}/doxygen/bin"
        "C:/Program Files/doxygen/bin"
    )
elseif(APPLE)
    set(_DOXYGEN_HINTS /opt/homebrew/bin /usr/local/bin /Applications/Doxygen.app/Contents/Resources)
else()
    set(_DOXYGEN_HINTS /usr/bin /usr/local/bin)
endif()
find_program(ASCIICHAT_DOXYGEN_EXECUTABLE
    NAMES doxygen
    HINTS ${_DOXYGEN_HINTS}
    DOC "Doxygen documentation generator"
)
unset(_DOXYGEN_HINTS)

# =============================================================================
# gmake - GNU Make (macOS)
# =============================================================================
if(APPLE)
    find_program(ASCIICHAT_GMAKE_EXECUTABLE
        NAMES gmake
        HINTS /opt/homebrew/bin /usr/local/bin
        DOC "GNU Make"
    )
endif()

# =============================================================================
# LLD linker (platform-specific names)
# =============================================================================
# LLD has different "flavors" for different platforms:
#   - ld.lld    : ELF linker (Linux)
#   - ld64.lld  : Mach-O linker (macOS) - mimics Apple's ld64
#   - lld-link  : COFF/PE linker (Windows)
# Clang's -fuse-ld=lld automatically selects the right flavor.
if(APPLE)
    _find_llvm_tool(ASCIICHAT_LLD_EXECUTABLE ld64.lld lld)
elseif(WIN32)
    _find_llvm_tool(ASCIICHAT_LLD_EXECUTABLE lld-link lld)
else()
    # Linux/Unix
    _find_llvm_tool(ASCIICHAT_LLD_EXECUTABLE ld.lld lld)
endif()

# =============================================================================
# Packaging tools (platform-specific)
# =============================================================================

# Windows packaging tools
if(WIN32)
    # NSIS - Nullsoft Scriptable Install System
    find_program(ASCIICHAT_NSIS_EXECUTABLE
        NAMES makensis
        PATHS
            "$ENV{ProgramFiles}/NSIS"
            "$ENV{ProgramFiles\(x86\)}/NSIS"
            "C:/Program Files/NSIS"
            "C:/Program Files (x86)/NSIS"
        PATH_SUFFIXES bin
        DOC "NSIS makensis installer creator"
    )

    # WiX v4+ (.NET tool)
    find_program(ASCIICHAT_WIX_EXECUTABLE
        NAMES wix
        HINTS
            "$ENV{USERPROFILE}/.dotnet/tools"
            "$ENV{ProgramFiles}/dotnet/tools"
            "$ENV{ProgramFiles}/WiX Toolset v6.0/bin"
            "$ENV{ProgramFiles}/WiX Toolset v5.0/bin"
            "$ENV{ProgramFiles}/WiX Toolset v4.0/bin"
        DOC "WiX v4+ .NET tool"
    )

    # WiX v3 tools (candle and light)
    find_program(ASCIICHAT_WIX_CANDLE_EXECUTABLE
        NAMES candle.exe candle
        PATHS
            "$ENV{WIX}/bin"
            "C:/Program Files (x86)/WiX Toolset v3.14/bin"
            "C:/Program Files/WiX Toolset v3.14/bin"
            "C:/Program Files (x86)/WiX Toolset v3.11/bin"
            "C:/Program Files/WiX Toolset v3.11/bin"
        DOC "WiX v3 candle compiler"
    )

    find_program(ASCIICHAT_WIX_LIGHT_EXECUTABLE
        NAMES light.exe light
        PATHS
            "$ENV{WIX}/bin"
            "C:/Program Files (x86)/WiX Toolset v3.14/bin"
            "C:/Program Files/WiX Toolset v3.14/bin"
            "C:/Program Files (x86)/WiX Toolset v3.11/bin"
            "C:/Program Files/WiX Toolset v3.11/bin"
        DOC "WiX v3 light linker"
    )

    # NMAKE - for building BearSSL on Windows
    # Search common Visual Studio installation paths
    find_program(ASCIICHAT_NMAKE_EXECUTABLE
        NAMES nmake
        HINTS
            "$ENV{VCToolsInstallDir}/bin/Hostx64/x64"
            "$ENV{VSINSTALLDIR}/VC/Tools/MSVC/*/bin/Hostx64/x64"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/*/bin/Hostx64/x64"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC/*/bin/Hostx64/x64"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC/*/bin/Hostx64/x64"
            "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/2019/Community/VC/Tools/MSVC/*/bin/Hostx64/x64"
        DOC "Microsoft NMAKE"
    )
endif()

# macOS packaging tools
if(APPLE)
    find_program(ASCIICHAT_PRODUCTBUILD_EXECUTABLE
        NAMES productbuild
        HINTS /usr/bin
        DOC "macOS productbuild installer creator"
    )
endif()

# Linux packaging tools
if(UNIX AND NOT APPLE)
    find_program(ASCIICHAT_DPKG_DEB_EXECUTABLE
        NAMES dpkg-deb
        HINTS /usr/bin /usr/local/bin
        DOC "Debian dpkg-deb package builder"
    )

    find_program(ASCIICHAT_RPMBUILD_EXECUTABLE
        NAMES rpmbuild
        HINTS /usr/bin /usr/local/bin
        DOC "RPM rpmbuild package builder"
    )

    # musl-gcc and gcc are required for Linux Release builds with USE_MUSL=ON
    # musl-gcc wraps gcc to build against musl libc instead of glibc
    find_program(ASCIICHAT_MUSL_GCC_EXECUTABLE
        NAMES musl-gcc
        HINTS /usr/bin /usr/local/bin
        DOC "musl-gcc C compiler wrapper for musl libc"
    )

    find_program(ASCIICHAT_GCC_EXECUTABLE
        NAMES gcc
        HINTS /usr/bin /usr/local/bin
        DOC "GCC C compiler (used by musl-gcc via REALGCC)"
    )
endif()

# =============================================================================
# Cleanup temporary variables
# =============================================================================
unset(_BASH_SEARCH_PATHS)

# =============================================================================
# Status summary (optional, controlled by ASCIICHAT_VERBOSE_FIND_PROGRAMS)
# =============================================================================
if(ASCIICHAT_VERBOSE_FIND_PROGRAMS)
    message(STATUS "FindPrograms: ASCIICHAT_LLVM_BINDIR = ${ASCIICHAT_LLVM_BINDIR}")
    message(STATUS "FindPrograms: ASCIICHAT_CCACHE_EXECUTABLE = ${ASCIICHAT_CCACHE_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_LLVM_CONFIG_EXECUTABLE = ${ASCIICHAT_LLVM_CONFIG_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_CLANG_EXECUTABLE = ${ASCIICHAT_CLANG_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE = ${ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_CLANG_TIDY_EXECUTABLE = ${ASCIICHAT_CLANG_TIDY_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_CLANG_FORMAT_EXECUTABLE = ${ASCIICHAT_CLANG_FORMAT_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_BASH_EXECUTABLE = ${ASCIICHAT_BASH_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_STRIP_EXECUTABLE = ${ASCIICHAT_STRIP_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_PYTHON3_EXECUTABLE = ${ASCIICHAT_PYTHON3_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_NINJA_EXECUTABLE = ${ASCIICHAT_NINJA_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_ZIP_EXECUTABLE = ${ASCIICHAT_ZIP_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_LLD_EXECUTABLE = ${ASCIICHAT_LLD_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_LLVM_AR_EXECUTABLE = ${ASCIICHAT_LLVM_AR_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_LLVM_RANLIB_EXECUTABLE = ${ASCIICHAT_LLVM_RANLIB_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_LLVM_NM_EXECUTABLE = ${ASCIICHAT_LLVM_NM_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_LLVM_STRINGS_EXECUTABLE = ${ASCIICHAT_LLVM_STRINGS_EXECUTABLE}")
    message(STATUS "FindPrograms: ASCIICHAT_DOXYGEN_EXECUTABLE = ${ASCIICHAT_DOXYGEN_EXECUTABLE}")
    if(WIN32)
        message(STATUS "FindPrograms: ASCIICHAT_CLANG_CL_EXECUTABLE = ${ASCIICHAT_CLANG_CL_EXECUTABLE}")
        message(STATUS "FindPrograms: ASCIICHAT_LLVM_LIB_EXECUTABLE = ${ASCIICHAT_LLVM_LIB_EXECUTABLE}")
        message(STATUS "FindPrograms: ASCIICHAT_NSIS_EXECUTABLE = ${ASCIICHAT_NSIS_EXECUTABLE}")
        message(STATUS "FindPrograms: ASCIICHAT_WIX_EXECUTABLE = ${ASCIICHAT_WIX_EXECUTABLE}")
        message(STATUS "FindPrograms: ASCIICHAT_WIX_CANDLE_EXECUTABLE = ${ASCIICHAT_WIX_CANDLE_EXECUTABLE}")
        message(STATUS "FindPrograms: ASCIICHAT_WIX_LIGHT_EXECUTABLE = ${ASCIICHAT_WIX_LIGHT_EXECUTABLE}")
        message(STATUS "FindPrograms: ASCIICHAT_NMAKE_EXECUTABLE = ${ASCIICHAT_NMAKE_EXECUTABLE}")
    endif()
    if(APPLE)
        message(STATUS "FindPrograms: ASCIICHAT_PRODUCTBUILD_EXECUTABLE = ${ASCIICHAT_PRODUCTBUILD_EXECUTABLE}")
        message(STATUS "FindPrograms: ASCIICHAT_GMAKE_EXECUTABLE = ${ASCIICHAT_GMAKE_EXECUTABLE}")
    endif()
    if(UNIX AND NOT APPLE)
        message(STATUS "FindPrograms: ASCIICHAT_DPKG_DEB_EXECUTABLE = ${ASCIICHAT_DPKG_DEB_EXECUTABLE}")
        message(STATUS "FindPrograms: ASCIICHAT_RPMBUILD_EXECUTABLE = ${ASCIICHAT_RPMBUILD_EXECUTABLE}")
        message(STATUS "FindPrograms: ASCIICHAT_MUSL_GCC_EXECUTABLE = ${ASCIICHAT_MUSL_GCC_EXECUTABLE}")
        message(STATUS "FindPrograms: ASCIICHAT_GCC_EXECUTABLE = ${ASCIICHAT_GCC_EXECUTABLE}")
    endif()
endif()

# =============================================================================
# Required Programs Validation
# =============================================================================
# Check for programs that are required for all builds.
# See docs/topics/build.dox for complete requirements documentation.
#
# Required for ALL builds:
#   - clang: C compiler (ascii-chat is Clang-only)
#   - clang++: C++ compiler (for defer/panic tools)
#   - llvm-config: queries LLVM paths (required by defer tool)
#   - llvm-ar: static library archiver
#   - llvm-ranlib: static library indexer
#
# Required for Windows only:
#   - llvm-lib: for BearSSL static library build
#
# Required for Linux Release builds with USE_MUSL=ON:
#   - musl-gcc: musl C compiler wrapper (builds static dependencies)
#   - gcc: used by musl-gcc via REALGCC environment variable
#   - lld: LLVM linker (for static-PIE linking)
#   (Validated in MuslDependencies.cmake when USE_MUSL is enabled)
#
# Required for panic builds:
#   - bash: runs panic instrumentation scripts
# =============================================================================

# clang is required for all builds (ascii-chat is a Clang-only project)
if(NOT ASCIICHAT_CLANG_EXECUTABLE)
    message(FATAL_ERROR "clang not found. Install LLVM/Clang:\n"
        "  Windows: winget install LLVM.LLVM  OR  scoop install llvm\n"
        "  macOS:   brew install llvm\n"
        "  Linux:   sudo apt install clang  OR  sudo pacman -S clang")
endif()

# clang++ is required for building the defer tool (defer() is used in codebase)
if(NOT ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE)
    message(FATAL_ERROR "clang++ not found. Required for building defer tool. Install LLVM/Clang:\n"
        "  Windows: winget install LLVM.LLVM  OR  scoop install llvm\n"
        "  macOS:   brew install llvm\n"
        "  Linux:   sudo apt install clang  OR  sudo pacman -S clang")
endif()

# llvm-config is required for defer tool to find LLVM libraries
if(NOT ASCIICHAT_LLVM_CONFIG_EXECUTABLE)
    message(FATAL_ERROR "llvm-config not found. Required for building defer tool. Install LLVM:\n"
        "  Windows: winget install LLVM.LLVM  OR  scoop install llvm\n"
        "  macOS:   brew install llvm\n"
        "  Linux:   sudo apt install llvm  OR  sudo pacman -S llvm")
endif()

# llvm-ar is required for static library archiving
if(NOT ASCIICHAT_LLVM_AR_EXECUTABLE)
    message(FATAL_ERROR "llvm-ar not found. Install LLVM:\n"
        "  Windows: winget install LLVM.LLVM  OR  scoop install llvm\n"
        "  macOS:   brew install llvm\n"
        "  Linux:   sudo apt install llvm  OR  sudo pacman -S llvm")
endif()

# llvm-ranlib is required for static library indexing
if(NOT ASCIICHAT_LLVM_RANLIB_EXECUTABLE)
    message(FATAL_ERROR "llvm-ranlib not found. Install LLVM:\n"
        "  Windows: winget install LLVM.LLVM  OR  scoop install llvm\n"
        "  macOS:   brew install llvm\n"
        "  Linux:   sudo apt install llvm  OR  sudo pacman -S llvm")
endif()

# Windows-specific: llvm-lib is required for BearSSL static library
if(WIN32 AND NOT ASCIICHAT_LLVM_LIB_EXECUTABLE)
    message(FATAL_ERROR "llvm-lib not found (required for BearSSL on Windows). Install LLVM:\n"
        "  winget install LLVM.LLVM  OR  scoop install llvm")
endif()

# Windows-specific: clang-cl is required for BearSSL build
if(WIN32 AND NOT ASCIICHAT_CLANG_CL_EXECUTABLE)
    message(FATAL_ERROR "clang-cl not found (required for BearSSL on Windows). Install LLVM:\n"
        "  winget install LLVM.LLVM  OR  scoop install llvm")
endif()

# Windows-specific: nmake is required for BearSSL build
if(WIN32 AND NOT ASCIICHAT_NMAKE_EXECUTABLE)
    message(FATAL_ERROR "nmake not found (required for BearSSL on Windows). Install Visual Studio Build Tools:\n"
        "  winget install Microsoft.VisualStudio.2022.BuildTools\n"
        "  OR download from: https://visualstudio.microsoft.com/visual-cpp-build-tools/")
endif()

# =============================================================================
# Optional Programs Validation Function
# =============================================================================
# This function validates programs that are only required when specific options
# are enabled. Call it after project() and after setting options like
# ASCIICHAT_ENABLE_ANALYZERS, ASCIICHAT_BUILD_WITH_PANIC, USE_MUSL, etc.
#
# Usage in CMakeLists.txt (after project() and option definitions):
#   asciichat_validate_optional_programs()
# =============================================================================
function(asciichat_validate_optional_programs)
    # ASCIICHAT_ENABLE_ANALYZERS requires clang-tidy
    if(ASCIICHAT_ENABLE_ANALYZERS AND NOT ASCIICHAT_CLANG_TIDY_EXECUTABLE)
        message(FATAL_ERROR "ASCIICHAT_ENABLE_ANALYZERS=ON but clang-tidy not found. Install clang-tidy:\n"
            "  Windows: winget install LLVM.LLVM  OR  scoop install llvm\n"
            "  macOS:   brew install llvm\n"
            "  Linux:   sudo apt install clang-tidy  OR  sudo pacman -S clang")
    endif()

    # ASCIICHAT_BUILD_WITH_PANIC requires bash for panic scripts
    if(ASCIICHAT_BUILD_WITH_PANIC AND NOT ASCIICHAT_BASH_EXECUTABLE)
        message(FATAL_ERROR "ASCIICHAT_BUILD_WITH_PANIC=ON but bash not found.\n"
            "  Windows: Install Git for Windows (includes Git Bash)\n"
            "  macOS/Linux: bash should be available at /bin/bash")
    endif()

    # USE_MUSL requires musl-gcc and gcc (Linux only)
    # Note: More detailed validation happens in MuslDependencies.cmake
    if(USE_MUSL)
        if(NOT ASCIICHAT_MUSL_GCC_EXECUTABLE)
            message(FATAL_ERROR "USE_MUSL=ON but musl-gcc not found. Install musl-tools:\n"
                "  Debian/Ubuntu: sudo apt install musl-tools\n"
                "  Arch: sudo pacman -S musl")
        endif()
        if(NOT ASCIICHAT_GCC_EXECUTABLE)
            message(FATAL_ERROR "USE_MUSL=ON but gcc not found (required by musl-gcc via REALGCC).\n"
                "  Debian/Ubuntu: sudo apt install gcc\n"
                "  Arch: sudo pacman -S gcc")
        endif()
        if(NOT ASCIICHAT_LLD_EXECUTABLE)
            message(FATAL_ERROR "USE_MUSL=ON but lld not found (required for static-PIE linking). Install LLVM:\n"
                "  Debian/Ubuntu: sudo apt install lld\n"
                "  Arch: sudo pacman -S lld")
        endif()
    endif()
endfunction()
