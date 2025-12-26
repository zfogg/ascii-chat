# =============================================================================
# LLVM Toolchain Configuration Module
# =============================================================================
# Determines LLVM root directory in ONE place using llvm-config.
# Trusts the PATH ordering: user ensures desired llvm-config is found first.
# All platforms (Windows, macOS, Linux) use the same approach.
#
# This module handles:
# - Compiler detection and selection via llvm-config
# - Clang resource directory configuration
# - macOS SDK path configuration
# - LLVM library paths and linking
# - LLVM tools (llvm-ar, llvm-ranlib)
#
# Functions:
#   - configure_llvm_pre_project(): Sets up compiler before project()
#   - configure_llvm_post_project(): Sets up library paths after project()
#   - find_llvm_tools(): Finds llvm-ar and llvm-ranlib
#   - fix_llvm_ranlib(): Fixes LLVM ranlib not being called automatically (after project())
#
# Prerequisites:
#   - configure_llvm_pre_project() must run before project()
#   - configure_llvm_post_project() must run after project()
#   - fix_llvm_ranlib() must run after project()
# =============================================================================

# =============================================================================
# Part 1: Pre-project() Configuration (Compiler Selection)
# =============================================================================
# SINGLE SOURCE OF TRUTH: Use llvm-config to determine LLVM root directory
# Trust the PATH: user ensures desired llvm-config is in PATH first
# =============================================================================

function(configure_llvm_pre_project)
    # Check for ccache and use it if available (from centralized FindPrograms.cmake)
    if(ASCIICHAT_CCACHE_EXECUTABLE)
        set(CMAKE_C_COMPILER_LAUNCHER "${ASCIICHAT_CCACHE_EXECUTABLE}" CACHE STRING "C compiler launcher" FORCE)
    else()
        if(DEFINED CMAKE_C_COMPILER_LAUNCHER AND CMAKE_C_COMPILER_LAUNCHER MATCHES "ccache")
            message(STATUS "${BoldYellow}ccache${ColorReset} not found; clearing compiler launcher.")
            unset(CMAKE_C_COMPILER_LAUNCHER CACHE)
        endif()
    endif()

    # =============================================================================
    # SINGLE SOURCE OF TRUTH: Determine LLVM root directory via llvm-config or clang path
    # Trust PATH: use FindPrograms.cmake result which respects user's PATH
    # =============================================================================
    set(LLVM_ROOT_PREFIX "")
    set(LLVM_CONFIG_EXECUTABLE "${ASCIICHAT_LLVM_CONFIG_EXECUTABLE}")

    if(LLVM_CONFIG_EXECUTABLE)
        # Get LLVM root directory from llvm-config (Unix/macOS)
        execute_process(
            COMMAND ${LLVM_CONFIG_EXECUTABLE} --prefix
            OUTPUT_VARIABLE LLVM_ROOT_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        if(NOT LLVM_ROOT_PREFIX OR NOT EXISTS "${LLVM_ROOT_PREFIX}/bin/clang")
            message(FATAL_ERROR "llvm-config reported LLVM at ${LLVM_ROOT_PREFIX}, but clang not found at ${LLVM_ROOT_PREFIX}/bin/clang\n"
                            "Verify llvm-config is working correctly:\n"
                            "  ${LLVM_CONFIG_EXECUTABLE} --prefix")
        endif()
    elseif(WIN32 AND ASCIICHAT_CLANG_EXECUTABLE)
        # Windows: derive LLVM root from clang executable location
        # Official pre-built LLVM on Windows doesn't include llvm-config
        get_filename_component(_clang_dir "${ASCIICHAT_CLANG_EXECUTABLE}" DIRECTORY)
        get_filename_component(LLVM_ROOT_PREFIX "${_clang_dir}" DIRECTORY)
        unset(_clang_dir)

        if(NOT EXISTS "${LLVM_ROOT_PREFIX}/bin/clang.exe")
            message(FATAL_ERROR "Failed to determine LLVM root directory from clang path: ${ASCIICHAT_CLANG_EXECUTABLE}\n"
                            "Expected clang at: ${LLVM_ROOT_PREFIX}/bin/clang.exe")
        endif()
        message(STATUS "${BoldGreen}Detected LLVM${ColorReset} root from clang: ${BoldCyan}${LLVM_ROOT_PREFIX}${ColorReset}")
    else()
        message(FATAL_ERROR "llvm-config not found in PATH. Install LLVM and ensure it's in PATH:\n"
                        "  Windows: winget install LLVM.LLVM  OR  scoop install llvm\n"
                        "  macOS: brew install llvm\n"
                        "  Debian/Ubuntu: sudo apt install llvm\n"
                        "  Fedora/RHEL: sudo dnf install llvm\n"
                        "  Arch: sudo pacman -S llvm")
    endif()

    message(STATUS "${BoldGreen}Found LLVM${ColorReset} at: ${BoldCyan}${LLVM_ROOT_PREFIX}${ColorReset}")

    # Get LLVM CMake directories for find_package()
    set(LLVM_CMAKEDIR "")
    if(LLVM_CONFIG_EXECUTABLE)
        execute_process(
            COMMAND ${LLVM_CONFIG_EXECUTABLE} --cmakedir
            OUTPUT_VARIABLE LLVM_CMAKEDIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    if(LLVM_CMAKEDIR AND EXISTS "${LLVM_CMAKEDIR}")
        message(STATUS "${BoldGreen}Found LLVM CMake${ColorReset} directory: ${BoldCyan}${LLVM_CMAKEDIR}${ColorReset}")
        list(APPEND CMAKE_PREFIX_PATH "${LLVM_ROOT_PREFIX}")
        list(APPEND CMAKE_PREFIX_PATH "${LLVM_CMAKEDIR}")
        set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
    else()
        # Fallback: add LLVM root and common CMake paths
        list(APPEND CMAKE_PREFIX_PATH "${LLVM_ROOT_PREFIX}")
        if(EXISTS "${LLVM_ROOT_PREFIX}/lib/cmake/llvm")
            list(APPEND CMAKE_PREFIX_PATH "${LLVM_ROOT_PREFIX}/lib/cmake/llvm")
        endif()
        set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
        if(LLVM_CONFIG_EXECUTABLE OR NOT WIN32)
            message(WARNING "${BoldYellow}Could not determine LLVM CMake directory${ColorReset}, using fallback paths")
        endif()
    endif()

    # Add Clang cmake directory as well
    if(EXISTS "${LLVM_ROOT_PREFIX}/lib/cmake/clang")
        list(APPEND CMAKE_PREFIX_PATH "${LLVM_ROOT_PREFIX}/lib/cmake/clang")
        set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
        message(STATUS "${BoldGreen}Found Clang CMake${ColorReset} directory: ${BoldCyan}${LLVM_ROOT_PREFIX}/lib/cmake/clang${ColorReset}")
    endif()

    # Export LLVM_ROOT_PREFIX to parent scope for use throughout cmake config
    set(LLVM_ROOT_PREFIX "${LLVM_ROOT_PREFIX}" PARENT_SCOPE)

    # =============================================================================
    # Set LLVM Tools (ar, ranlib) before project()
    # Must happen here so archive rules use llvm-ar, not system ar
    # =============================================================================
    # ASCIICHAT_LLVM_AR_EXECUTABLE was found by FindPrograms.cmake
    if(ASCIICHAT_LLVM_AR_EXECUTABLE)
        set(CMAKE_AR "${ASCIICHAT_LLVM_AR_EXECUTABLE}" CACHE FILEPATH "Archiver" FORCE)
        message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}llvm-ar${ColorReset} archiver: ${BoldCyan}${CMAKE_AR}${ColorReset}")
    endif()

    if(ASCIICHAT_LLVM_RANLIB_EXECUTABLE)
        set(CMAKE_RANLIB "${ASCIICHAT_LLVM_RANLIB_EXECUTABLE}" CACHE FILEPATH "Ranlib" FORCE)
        message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}llvm-ranlib${ColorReset} archiver: ${BoldCyan}${CMAKE_RANLIB}${ColorReset}")
    endif()

    # =============================================================================
    # Compiler Detection (before project())
    # Use the LLVM_ROOT_PREFIX determined above
    # =============================================================================
    if(NOT CMAKE_C_COMPILER)
        # Determine clang executable name based on platform
        if(WIN32)
            set(CLANG_EXECUTABLE "${LLVM_ROOT_PREFIX}/bin/clang.exe")
            set(CLANGXX_EXECUTABLE "${LLVM_ROOT_PREFIX}/bin/clang++.exe")
        else()
            set(CLANG_EXECUTABLE "${LLVM_ROOT_PREFIX}/bin/clang")
            set(CLANGXX_EXECUTABLE "${LLVM_ROOT_PREFIX}/bin/clang++")
        endif()

        # Verify clang executables exist
        if(NOT EXISTS "${CLANG_EXECUTABLE}")
            message(FATAL_ERROR "${BoldRed}Clang not found${ColorReset} at ${CLANG_EXECUTABLE}\n"
                            "LLVM root: ${LLVM_ROOT_PREFIX}\n"
                            "llvm-config: ${LLVM_CONFIG_EXECUTABLE}")
        endif()

        if(NOT EXISTS "${CLANGXX_EXECUTABLE}")
            message(FATAL_ERROR "${BoldRed}Clang++ not found${ColorReset} at ${CLANGXX_EXECUTABLE}\n"
                            "LLVM root: ${LLVM_ROOT_PREFIX}\n"
                            "llvm-config: ${LLVM_CONFIG_EXECUTABLE}")
        endif()

        # Set the compilers
        set(CMAKE_C_COMPILER "${CLANG_EXECUTABLE}" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "${CLANGXX_EXECUTABLE}" CACHE FILEPATH "CXX compiler" FORCE)
        message(STATUS "Set ${BoldYellow}C${ColorReset} compiler: ${BoldCyan}${CLANG_EXECUTABLE}${ColorReset}")
        message(STATUS "Set ${BoldYellow}C++${ColorReset} compiler: ${BoldCyan}${CLANGXX_EXECUTABLE}${ColorReset}")
    else()
        # C compiler is already set, ensure CXX compiler is also set
        if(NOT CMAKE_CXX_COMPILER)
            message(STATUS "${BoldYellow}C${ColorReset} compiler already set: ${BoldCyan}${CMAKE_C_COMPILER}${ColorReset}")

            # Derive CXX compiler from the existing C compiler
            if(CMAKE_C_COMPILER MATCHES "clang\\.exe$")
                string(REPLACE "clang.exe" "clang++.exe" CLANGXX_EXECUTABLE "${CMAKE_C_COMPILER}")
            elseif(CMAKE_C_COMPILER MATCHES "clang(-[0-9]+)?$")
                set(CLANGXX_EXECUTABLE "${CMAKE_C_COMPILER}++")
            else()
                get_filename_component(COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
                if(EXISTS "${COMPILER_DIR}/clang++${CMAKE_EXECUTABLE_SUFFIX}")
                    set(CLANGXX_EXECUTABLE "${COMPILER_DIR}/clang++${CMAKE_EXECUTABLE_SUFFIX}")
                endif()
            endif()

            if(CLANGXX_EXECUTABLE AND EXISTS "${CLANGXX_EXECUTABLE}")
                set(CMAKE_CXX_COMPILER "${CLANGXX_EXECUTABLE}" CACHE FILEPATH "CXX compiler" FORCE)
                message(STATUS "Auto-detected ${BoldYellow}C++${ColorReset} compiler: ${BoldCyan}${CLANGXX_EXECUTABLE}${ColorReset}")
            else()
                message(FATAL_ERROR "Could not find clang++ compiler. Please install LLVM/Clang.")
            endif()
        endif()
    endif()
endfunction()

# =============================================================================
# Part 2: Post-project() Configuration (Library Paths)
# =============================================================================
# This section must run after project() to configure linking
# =============================================================================

function(configure_llvm_post_project)
    if(NOT APPLE)
        return()
    endif()

    # Allow temp compilation database builds to keep CMAKE_OSX_SYSROOT for tool integration
    set(_keep_osx_sysroot_for_tools FALSE)
    if(ASCIICHAT_KEEP_SYSROOT_FOR_TOOLS)
        set(_keep_osx_sysroot_for_tools TRUE)
    endif()

    # Determine LLVM_ROOT_PREFIX from CMAKE_C_COMPILER or from LLVM_ROOT_PREFIX cache
    set(LLVM_ROOT_PREFIX "")
    if(CMAKE_C_COMPILER)
        get_filename_component(COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(LLVM_ROOT_PREFIX "${COMPILER_DIR}" DIRECTORY)
    endif()

    if(NOT LLVM_ROOT_PREFIX OR NOT EXISTS "${LLVM_ROOT_PREFIX}/bin/clang")
        message(WARNING "Could not determine LLVM root directory from compiler path: ${CMAKE_C_COMPILER}")
        return()
    endif()

    # Determine LLVM source name for display purposes only
    # (all sources are detected via LLVM_ROOT_PREFIX from llvm-config)
    set(LLVM_SOURCE_NAME "LLVM")
    if(LLVM_ROOT_PREFIX MATCHES "/opt/homebrew/opt/llvm" OR LLVM_ROOT_PREFIX MATCHES "/usr/local/opt/llvm")
        set(LLVM_SOURCE_NAME "Homebrew LLVM")
    elseif(LLVM_ROOT_PREFIX MATCHES "/usr/local$")
        set(LLVM_SOURCE_NAME "git-built LLVM")
    endif()

    # Configure resource directory
    execute_process(
        COMMAND "${LLVM_ROOT_PREFIX}/bin/clang" -print-resource-dir
        OUTPUT_VARIABLE CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Fallback: construct from version if -print-resource-dir failed
    if(NOT CLANG_RESOURCE_DIR OR NOT EXISTS "${CLANG_RESOURCE_DIR}/include")
        execute_process(
            COMMAND "${LLVM_ROOT_PREFIX}/bin/clang" --version
            OUTPUT_VARIABLE CLANG_VERSION_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REGEX MATCH "clang version ([0-9]+)\\.([0-9]+)" CLANG_VERSION_MATCH "${CLANG_VERSION_OUTPUT}")
        set(CLANG_MAJOR_VERSION "${CMAKE_MATCH_1}")
        set(CLANG_RESOURCE_DIR "${LLVM_ROOT_PREFIX}/lib/clang/${CLANG_MAJOR_VERSION}")
    endif()

    if(EXISTS "${CLANG_RESOURCE_DIR}/include")
        message(STATUS "${BoldGreen}Found${ColorReset} ${BoldBlue}Clang${ColorReset} resource directory: ${CLANG_RESOURCE_DIR}")
        # Note: Do NOT add -resource-dir to CMAKE_*_FLAGS as it would hardcode absolute paths
        # in the compilation database, breaking builds on systems with different LLVM installation paths.
        # Clang finds its resource directory automatically without explicit flags.
        # For LibTooling-based tools (defer, panic, query), we will pass the resource directory explicitly
        # in their invocation, not in the compilation database itself.
        message(STATUS "Include: (using compiler's resource directory - NOT added globally)")
    else()
        message(WARNING "${BoldYellow}Could not find Clang resource directory${ColorReset} at: ${CLANG_RESOURCE_DIR}")
    endif()

    # macOS SDK handling:
    # - Self-contained LLVM (Homebrew, git-built): clang finds headers automatically, don't use -isysroot
    # - System clang: needs Apple's SDK path to find system headers
    # IMPORTANT: Save SDK path BEFORE clearing CMAKE_OSX_SYSROOT, so compilation database generation can use it
    set(_macos_sdk_for_tools "")

    if(CMAKE_OSX_SYSROOT)
        set(_macos_sdk_for_tools "${CMAKE_OSX_SYSROOT}")
    elseif(DEFINED ENV{HOMEBREW_SDKROOT})
        # Homebrew sets HOMEBREW_SDKROOT in the environment
        set(_macos_sdk_for_tools "$ENV{HOMEBREW_SDKROOT}")
    else()
        # If not set yet, try to detect via xcrun
        find_program(_XCRUN_EXECUTABLE xcrun)
        if(_XCRUN_EXECUTABLE)
            execute_process(
                COMMAND ${_XCRUN_EXECUTABLE} --show-sdk-path
                OUTPUT_VARIABLE _detected_sdk_path
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_detected_sdk_path AND EXISTS "${_detected_sdk_path}")
                set(_macos_sdk_for_tools "${_detected_sdk_path}")
            endif()
            unset(_XCRUN_EXECUTABLE)
        endif()
    endif()

    if(_macos_sdk_for_tools)
        set(ASCIICHAT_MACOS_SDK_FOR_TOOLS "${_macos_sdk_for_tools}" CACHE INTERNAL "Saved macOS SDK path for tools (defer, panic, etc.)")
    endif()

    # Only clear CMAKE_OSX_SYSROOT for main build; temp builds for compilation database generation
    # should keep it so that tool integration (defer, panic, query) can find system headers
    if(NOT _keep_osx_sysroot_for_tools)
        set(CMAKE_OSX_SYSROOT "" CACHE STRING "macOS SDK root" FORCE)
        message(STATUS "${BoldGreen}Using${ColorReset} self-contained ${BoldBlue}${LLVM_SOURCE_NAME}${ColorReset}: disabling SDK root (-isysroot)")
    else()
        message(STATUS "${BoldGreen}Keeping${ColorReset} ${BoldBlue}CMAKE_OSX_SYSROOT${ColorReset} for tool integration (defer, panic, etc.)")
    endif()

    # Add library paths and linking for the detected LLVM installation
    # (determined via LLVM_ROOT_PREFIX from llvm-config)
    set(DETECTED_LLVM_LINK_FLAGS "")

    # Check library layout: Homebrew uses subdirectories, git-built and others use flat lib/
    if(EXISTS "${LLVM_ROOT_PREFIX}/lib/unwind" AND EXISTS "${LLVM_ROOT_PREFIX}/lib/c++")
        # Homebrew layout: lib/unwind/ and lib/c++/
        set(DETECTED_LLVM_LINK_DIRS "-L${LLVM_ROOT_PREFIX}/lib/unwind -L${LLVM_ROOT_PREFIX}/lib/c++")
        message(STATUS "${BoldGreen}Detected${ColorReset} Homebrew library layout (lib/unwind + lib/c++)")
    elseif(EXISTS "${LLVM_ROOT_PREFIX}/lib/libunwind.a" OR EXISTS "${LLVM_ROOT_PREFIX}/lib/libunwind.dylib")
        # Git-built or other layout: lib/ (flat)
        set(DETECTED_LLVM_LINK_DIRS "-L${LLVM_ROOT_PREFIX}/lib")
        message(STATUS "${BoldGreen}Detected${ColorReset} flat library layout (lib/)")
    else()
        message(WARNING "Could not detect LLVM library layout, defaulting to ${LLVM_ROOT_PREFIX}/lib")
        set(DETECTED_LLVM_LINK_DIRS "-L${LLVM_ROOT_PREFIX}/lib")
    endif()

    set(DETECTED_LLVM_LINK_FLAGS "${DETECTED_LLVM_LINK_DIRS} -lunwind")

    if(DETECTED_LLVM_LINK_FLAGS AND (NOT DEFINED ENV{LDFLAGS} OR NOT "$ENV{LDFLAGS}" MATCHES "-L.*llvm"))
        if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "-L.*llvm/lib")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${DETECTED_LLVM_LINK_FLAGS}" CACHE STRING "Linker flags" FORCE)
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${DETECTED_LLVM_LINK_FLAGS}" CACHE STRING "Shared linker flags" FORCE)
            message(STATUS "${BoldGreen}Added${ColorReset} ${BoldBlue}${LLVM_SOURCE_NAME}${ColorReset} library paths and -lunwind")
        else()
            message(STATUS "${BoldYellow}${LLVM_SOURCE_NAME}${ColorReset} library paths already present in linker flags")
        endif()
    else()
        message(STATUS "${BoldBlue}${LLVM_SOURCE_NAME}${ColorReset} library paths ${BoldGreen}already present${ColorReset} in LDFLAGS environment variable")
    endif()

    # Add LLVM library paths to rpath for Debug/Dev builds (for dynamic linking)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        set(LLVM_RPATH_UNWIND "")
        set(LLVM_RPATH_CXX "")

        if(EXISTS "${LLVM_ROOT_PREFIX}/lib/unwind" AND EXISTS "${LLVM_ROOT_PREFIX}/lib/c++")
            set(LLVM_RPATH_UNWIND "${LLVM_ROOT_PREFIX}/lib/unwind")
            set(LLVM_RPATH_CXX "${LLVM_ROOT_PREFIX}/lib/c++")
        elseif(EXISTS "${LLVM_ROOT_PREFIX}/lib/libunwind.dylib")
            set(LLVM_RPATH_UNWIND "${LLVM_ROOT_PREFIX}/lib")
            set(LLVM_RPATH_CXX "${LLVM_ROOT_PREFIX}/lib")
        endif()

        if(LLVM_RPATH_UNWIND AND NOT "${LLVM_RPATH_UNWIND}" IN_LIST CMAKE_BUILD_RPATH)
            list(APPEND CMAKE_BUILD_RPATH "${LLVM_RPATH_UNWIND}")
            set(CMAKE_BUILD_RPATH "${CMAKE_BUILD_RPATH}" CACHE INTERNAL "Build RPATH" FORCE)
            message(STATUS "${BoldGreen}Added${ColorReset} ${BoldBlue}LLVM unwind${ColorReset} to build rpath: ${BoldCyan}${LLVM_RPATH_UNWIND}${ColorReset}")
        endif()

        if(LLVM_RPATH_CXX AND NOT LLVM_RPATH_CXX STREQUAL LLVM_RPATH_UNWIND AND NOT "${LLVM_RPATH_CXX}" IN_LIST CMAKE_BUILD_RPATH)
            list(APPEND CMAKE_BUILD_RPATH "${LLVM_RPATH_CXX}")
            set(CMAKE_BUILD_RPATH "${CMAKE_BUILD_RPATH}" CACHE INTERNAL "Build RPATH" FORCE)
            message(STATUS "${BoldGreen}Added${ColorReset} ${BoldBlue}LLVM c++${ColorReset} to build rpath: ${BoldCyan}${LLVM_RPATH_CXX}${ColorReset}")
        endif()
    endif()

    message(STATUS "${BoldGreen}Applied${ColorReset} ${BoldBlue}${LLVM_SOURCE_NAME}${ColorReset} toolchain flags:")
    message(STATUS "  Include: (using compiler's resource directory - NOT added globally)")
    if(HOMEBREW_LLVM_LINK_FLAGS AND (NOT DEFINED ENV{LDFLAGS} OR NOT "$ENV{LDFLAGS}" MATCHES "-L.*llvm"))
        message(STATUS "  Link: ${BoldCyan}${HOMEBREW_LLVM_LINK_FLAGS}${ColorReset}")
    else()
        message(STATUS "  Link: from ${BoldCyan}LDFLAGS${ColorReset}=${BoldYellow}$ENV{LDFLAGS}${ColorReset} environment variable")
    endif()
endfunction()

# =============================================================================
# Helper Function: Find LLVM Tools (llvm-ar, llvm-ranlib)
# =============================================================================
# This function finds llvm-ar and llvm-ranlib using llvm-config or common paths.
# Can be called from any module that needs LLVM tools.
#
# Outputs:
#   - CMAKE_AR set to llvm-ar if found
#   - CMAKE_RANLIB set to llvm-ranlib if found
# =============================================================================

function(find_llvm_tools)
    # Use centralized LLVM tools from FindPrograms.cmake
    # These were already found during early initialization
    #
    # NOTE: CMAKE_AR and CMAKE_RANLIB are auto-detected by CMake during project(),
    # and cache entries are created. We must set them WITHOUT the CACHE keyword to
    # override the cached values, then update the cache variables manually.

    if(ASCIICHAT_LLVM_AR_EXECUTABLE)
        # Set CMAKE_AR both as regular variable AND update cache
        set(CMAKE_AR "${ASCIICHAT_LLVM_AR_EXECUTABLE}")
        set(CMAKE_AR "${ASCIICHAT_LLVM_AR_EXECUTABLE}" CACHE FILEPATH "Archiver" FORCE)
        message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}llvm-ar${ColorReset} archiver: ${BoldCyan}${CMAKE_AR}${ColorReset}")
    endif()

    if(ASCIICHAT_LLVM_RANLIB_EXECUTABLE)
        # Set CMAKE_RANLIB both as regular variable AND update cache
        set(CMAKE_RANLIB "${ASCIICHAT_LLVM_RANLIB_EXECUTABLE}")
        set(CMAKE_RANLIB "${ASCIICHAT_LLVM_RANLIB_EXECUTABLE}" CACHE FILEPATH "Ranlib" FORCE)
        message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}llvm-ranlib${ColorReset} archiver: ${BoldCyan}${CMAKE_RANLIB}${ColorReset}")
    endif()
endfunction()

# =============================================================================
# Helper Function: Fix LLVM Ranlib Not Being Called Automatically
# =============================================================================
# This function fixes LLVM ranlib not being called automatically (must be after project()).
# This prevents "archive has no index" link errors.
#
# Prerequisites:
#   - Must run after project()
# =============================================================================

function(fix_llvm_ranlib)
    if(CMAKE_RANLIB)
        set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> qc <TARGET> <LINK_FLAGS> <OBJECTS>")
        set(CMAKE_C_ARCHIVE_APPEND "<CMAKE_AR> q <TARGET> <LINK_FLAGS> <OBJECTS>")
        set(CMAKE_C_ARCHIVE_FINISH "<CMAKE_RANLIB> <TARGET>")
        set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qc <TARGET> <LINK_FLAGS> <OBJECTS>")
        set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> q <TARGET> <LINK_FLAGS> <OBJECTS>")
        set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> <TARGET>")
    endif()
endfunction()
