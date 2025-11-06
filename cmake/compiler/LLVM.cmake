# =============================================================================
# LLVM Toolchain Configuration Module
# =============================================================================
# Auto-detects and configures Homebrew LLVM (if installed) as the compiler
# toolchain on macOS, providing full LLVM/Clang features and tools.
# Also provides LLVM tools (llvm-ar, llvm-ranlib, llvm-config) for any platform.
#
# This module handles:
# - Compiler detection and selection
# - Clang resource directory configuration
# - macOS SDK path configuration
# - LLVM library paths and linking
# - LLVM tools (llvm-ar, llvm-ranlib, llvm-config)
# - LLVM ranlib fix for archive handling
#
# Functions:
#   - configure_llvm_pre_project(): Sets up compiler before project()
#   - configure_llvm_post_project(): Sets up library paths after project()
#   - find_llvm_tools(): Finds llvm-ar and llvm-ranlib using llvm-config
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
# This section must run before project() to set CMAKE_C_COMPILER
# =============================================================================

if (WIN32)
    set(LLVM_ROOT_DIRS "$ENV{PROGRAMFILES}/LLVM" "$ENV{LOCALAPPDATA}/Programs/LLVM/bin" "C:/Program Files/LLVM/bin" "$ENV{USERPROFILE}/scoop/apps/llvm/current/bin" "$ENV{USERPROFILE}/scoop/shims")
elseif(APPLE)
    set(LLVM_ROOT_DIRS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /usr/bin)
elseif(LINUX)
    set(LLVM_ROOT_DIRS /usr/local/bin /usr/lib/llvm/bin /usr/lib/llvm-21/bin /usr/lib/llvm-20/bin /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin /usr/lib/llvm-17/bin /usr/lib/llvm-16/bin /usr/lib/llvm-15/bin)
endif()

function(configure_llvm_pre_project)
    # Check for ccache and use it if available
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
    endif()

    # Compiler Detection (before project())
    # Force Clang compiler - MSVC and GCC are not supported
    # Supports Windows (scoop/official LLVM), macOS (Homebrew), and Linux (system)
    if(NOT CMAKE_C_COMPILER)
        if(WIN32)
            # Windows: Try to find Clang from common installation locations
            find_program(CLANG_EXECUTABLE
                NAMES clang clang.exe
                PATHS ${LLVM_ROOT_DIRS}
            )
            if(NOT CLANG_EXECUTABLE)
                message(FATAL_ERROR "Clang not found. Install Clang:\n"
                                "  Windows: scoop install llvm\n"
                                "  Or download from: https://llvm.org/builds/")
            endif()
        elseif(APPLE)
            # macOS: Prefer Homebrew Clang over Apple's Clang
            find_program(CLANG_EXECUTABLE
                NAMES clang
                PATHS ${LLVM_ROOT_DIRS}
            )

            if(NOT CLANG_EXECUTABLE)
                # Fallback to system clang if Homebrew not found
                find_program(CLANG_EXECUTABLE NAMES clang
                    PATHS ${LLVM_ROOT_DIRS}
                )
            endif()

            if(NOT CLANG_EXECUTABLE)
                message(FATAL_ERROR "Clang not found. Install Clang:\n"
                                "  macOS: brew install llvm\n"
                                "  Or use system clang from Xcode Command Line Tools")
            endif()
        else()
            # Linux/Unix: Use system Clang (support versions 21 down to 15)
            find_program(CLANG_EXECUTABLE
                NAMES clang clang-21 clang-20 clang-19 clang-18 clang-17 clang-16 clang-15
                PATHS ${LLVM_ROOT_DIRS}
            )

            if(NOT CLANG_EXECUTABLE)
                message(FATAL_ERROR "Clang not found. Install Clang:\n"
                                "  Debian/Ubuntu: sudo apt install clang\n"
                                "  Fedora/RHEL: sudo dnf install clang\n"
                                "  Arch: sudo pacman -S clang")
            endif()
        endif()

        # Set the compiler (all platforms)
        set(CMAKE_C_COMPILER "${CLANG_EXECUTABLE}" CACHE FILEPATH "C compiler" FORCE)

        # Derive CXX compiler path from C compiler
        if(WIN32)
            # Windows: Replace clang.exe with clang++.exe
            string(REPLACE "clang.exe" "clang++.exe" CLANGXX_EXECUTABLE "${CLANG_EXECUTABLE}")
        else()
            # Unix/macOS: Append ++ to clang
            set(CLANGXX_EXECUTABLE "${CLANG_EXECUTABLE}++")
        endif()

        set(CMAKE_CXX_COMPILER "${CLANGXX_EXECUTABLE}" CACHE FILEPATH "CXX compiler" FORCE)
        message(STATUS "Set default ${BoldYellow}C${ColorReset} compiler to ${BoldCyan}Clang${ColorReset}: ${CLANG_EXECUTABLE}")
        message(STATUS "Set default ${BoldYellow}C++${ColorReset} compiler to ${BoldCyan}Clang++${ColorReset}: ${CLANGXX_EXECUTABLE}")
    endif()

    # =============================================================================
    # Default to Clang on all platforms (before macOS-specific Homebrew LLVM)
    # =============================================================================
    # Only set compiler if not already set by user or environment
    if(NOT CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
        find_program(CLANG_EXECUTABLE clang clang-21 clang-20 clang-19 clang-18 clang-17 clang-16 clang-15
            PATHS ${LLVM_ROOT_DIRS}
        )
        if(CLANG_EXECUTABLE)
            set(CMAKE_C_COMPILER "${CLANG_EXECUTABLE}" CACHE FILEPATH "Default C compiler" FORCE)
        else()
            message(FATAL_ERROR "${BoldRed}Clang not found in PATH.${ColorReset} The project is designed for ${BoldGreen}Clang${ColorReset}.")
        endif()
    endif()

    # =============================================================================
    # macOS-specific Homebrew LLVM configuration
    # =============================================================================
    if(NOT APPLE)
        return()
    endif()

    # Check if Homebrew LLVM is installed
    set(HOMEBREW_LLVM_PREFIX "")

    # Check common Homebrew installation paths
    if(EXISTS "/usr/local/opt/llvm/bin/clang")
        set(HOMEBREW_LLVM_PREFIX "/usr/local/opt/llvm")
    elseif(EXISTS "/opt/homebrew/opt/llvm/bin/clang")
        set(HOMEBREW_LLVM_PREFIX "/opt/homebrew/opt/llvm")
    endif()

    # Set the compiler (all platforms)
    set(CMAKE_C_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang" CACHE FILEPATH "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang++" CACHE FILEPATH "CXX compiler" FORCE)
    set(CMAKE_OBJC_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang" CACHE FILEPATH "Objective-C compiler" FORCE)


    if(HOMEBREW_LLVM_PREFIX)
        message(STATUS "${BoldGreen}Found${ColorReset} ${BoldPurple}Homebrew LLVM${ColorReset} at: ${BoldCyan}${HOMEBREW_LLVM_PREFIX}${ColorReset}")
        set(USE_HOMEBREW_LLVM TRUE)

        if(USE_HOMEBREW_LLVM)
            # Check if compiler is already set to Homebrew LLVM
            if(CMAKE_C_COMPILER MATCHES "${HOMEBREW_LLVM_PREFIX}")
                message(STATUS "${BoldGreen}Using${ColorReset} user-specified ${BoldBlue}Homebrew LLVM${ColorReset} compiler: ${BoldCyan}${CMAKE_C_COMPILER}${ColorReset}")
            else()
                message(STATUS "Configuring to use ${BoldBlue}Homebrew LLVM${ColorReset}")
            endif()

            # Add LLVM bin directory to PATH for tools like llvm-ar, llvm-ranlib, etc.
            set(CMAKE_PREFIX_PATH "${HOMEBREW_LLVM_PREFIX}" ${CMAKE_PREFIX_PATH} PARENT_SCOPE)

            # Add LLVM CMake modules to module path for advanced features
            if(EXISTS "${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm")
                list(APPEND CMAKE_MODULE_PATH "${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm")
                message(STATUS "${BoldGreen}Added${ColorReset} ${BoldPurple}LLVM CMake${BoldBlue} modules:${ColorReset} ${BoldCyan}$${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm${ColorReset}")
            endif()

            # Set LLVM tool paths
            set(CMAKE_AR "${HOMEBREW_LLVM_PREFIX}/bin/llvm-ar" CACHE FILEPATH "Archiver" FORCE)
            set(CMAKE_RANLIB "${HOMEBREW_LLVM_PREFIX}/bin/llvm-ranlib" CACHE FILEPATH "Ranlib" FORCE)

            # Configure resource directory BEFORE project() so flags are set early
            # Get Clang version to construct resource directory path
            execute_process(
                COMMAND "${HOMEBREW_LLVM_PREFIX}/bin/clang" --version
                OUTPUT_VARIABLE CLANG_VERSION_OUTPUT
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            string(REGEX MATCH "clang version ([0-9]+)\\.([0-9]+)" CLANG_VERSION_MATCH "${CLANG_VERSION_OUTPUT}")
            set(CLANG_MAJOR_VERSION "${CMAKE_MATCH_1}")

            # Construct resource directory (don't trust -print-resource-dir, it returns wrong path)
            set(CLANG_RESOURCE_DIR "${HOMEBREW_LLVM_PREFIX}/lib/clang/${CLANG_MAJOR_VERSION}")

            if(EXISTS "${CLANG_RESOURCE_DIR}/include")
                message(STATUS "${BoldGreen}Found${ColorReset} ${BoldBlue}Clang${ColorReset} resource directory: ${CLANG_RESOURCE_DIR}")
                # Append to CMAKE_*_FLAGS so it takes effect for project() and all subdirectories (including mimalloc)
                string(APPEND CMAKE_C_FLAGS " -resource-dir ${CLANG_RESOURCE_DIR}")
                string(APPEND CMAKE_CXX_FLAGS " -resource-dir ${CLANG_RESOURCE_DIR}")
                string(APPEND CMAKE_OBJC_FLAGS " -resource-dir ${CLANG_RESOURCE_DIR}")
                # Export to parent scope
                set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
                set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS}" PARENT_SCOPE)
            else()
                message(WARNING "${BoldRed}Could not find Clang resource directory at: ${CLANG_RESOURCE_DIR}${ColorReset}")
            endif()

            # Get macOS SDK path for standard headers
            execute_process(
                COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE MACOS_SDK_PATH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )

            if(MACOS_SDK_PATH)
                message(STATUS "${BoldGreen}Found${ColorReset} ${BoldYellow}macOS SDK${ColorReset} at: ${BoldCyan}${MACOS_SDK_PATH}${ColorReset}")
                # Append SDK flags to CMAKE_*_FLAGS before project()
                string(APPEND CMAKE_C_FLAGS " -isysroot ${MACOS_SDK_PATH}")
                string(APPEND CMAKE_CXX_FLAGS " -isysroot ${MACOS_SDK_PATH}")
                string(APPEND CMAKE_OBJC_FLAGS " -isysroot ${MACOS_SDK_PATH}")
                string(APPEND CMAKE_EXE_LINKER_FLAGS " -isysroot ${MACOS_SDK_PATH}")
                string(APPEND CMAKE_SHARED_LINKER_FLAGS " -isysroot ${MACOS_SDK_PATH}")
                # Export to parent scope
                set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
                set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS}" PARENT_SCOPE)
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" PARENT_SCOPE)
                set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}" PARENT_SCOPE)
            endif()

            message(STATUS "${BoldGreen}Configured${ColorReset} ${BoldBlue}Homebrew LLVM${ColorReset}: ${BoldCyan}${CMAKE_C_COMPILER}${ColorReset}")
        endif()
    else()
        message(STATUS "${BoldBlue}Homebrew LLVM${ColorReset} not found, using system compiler")
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

    # Check if we're using Homebrew LLVM (either auto-detected or user-specified)
    if(CMAKE_C_COMPILER)
        get_filename_component(COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(COMPILER_PREFIX "${COMPILER_DIR}" DIRECTORY)

        # Also check for ccache wrapper pointing to Homebrew LLVM
        set(IS_HOMEBREW_LLVM FALSE)
        if(COMPILER_PREFIX MATCHES "/opt/homebrew/opt/llvm" OR COMPILER_PREFIX MATCHES "/usr/local/opt/llvm")
            set(IS_HOMEBREW_LLVM TRUE)
        elseif(COMPILER_PREFIX MATCHES "ccache" AND EXISTS "/usr/local/opt/llvm/bin/clang")
            set(COMPILER_PREFIX "/usr/local/opt/llvm")
            set(IS_HOMEBREW_LLVM TRUE)
        elseif(COMPILER_PREFIX MATCHES "ccache" AND EXISTS "/opt/homebrew/opt/llvm/bin/clang")
            set(COMPILER_PREFIX "/opt/homebrew/opt/llvm")
            set(IS_HOMEBREW_LLVM TRUE)
        endif()

        if(IS_HOMEBREW_LLVM)
            # Add LLVM library paths and libraries (equivalent to LDFLAGS from brew info llvm)
            link_directories("${COMPILER_PREFIX}/lib")

            # Add the full LDFLAGS as recommended by brew info llvm
            # This includes libc++, libunwind which are needed for full LLVM toolchain
            # For Release builds: Link libunwind statically
            # For Debug/Dev builds: Link dynamically for faster iteration
            if(CMAKE_BUILD_TYPE STREQUAL "Release")
                # Use absolute path to static libunwind.a for Release builds
                if(EXISTS "${COMPILER_PREFIX}/lib/unwind/libunwind.a")
                    set(HOMEBREW_LLVM_LINK_FLAGS "${COMPILER_PREFIX}/lib/unwind/libunwind.a")
                    message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}static libunwind${ColorReset}: ${HOMEBREW_LLVM_LINK_FLAGS}")
                else()
                    message(FATAL_ERROR "Could not find static ${BoldRed}libunwind.a${ColorReset} in ${COMPILER_PREFIX}/lib/unwind/")
                endif()
            else()
                # Debug/Dev builds: Use dynamic linking for faster development
                set(HOMEBREW_LLVM_LINK_DIRS "-L${COMPILER_PREFIX}/lib/unwind -L${COMPILER_PREFIX}/lib/c++ -L${COMPILER_PREFIX}/lib/unwind")
                set(HOMEBREW_LLVM_LINK_FLAGS "${HOMEBREW_LLVM_LINK_DIRS} -lunwind")
            endif()

            # Export to cache for later use
            set(HOMEBREW_LLVM_LIB_DIR "${COMPILER_PREFIX}/lib" CACHE INTERNAL "Homebrew LLVM library directory")

            if(NOT DEFINED ENV{LDFLAGS} OR NOT "$ENV{LDFLAGS}" MATCHES "-L.*llvm")
                # Add library search paths and -lunwind flag globally
                # Check if paths are already present to avoid duplication
                if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "-L.*llvm/lib/unwind")
                    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${HOMEBREW_LLVM_LINK_FLAGS}" CACHE STRING "Linker flags" FORCE)
                    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${HOMEBREW_LLVM_LINK_FLAGS}" CACHE STRING "Shared linker flags" FORCE)
                    message(STATUS "${BoldGreen}Added${ColorReset} ${BoldBlue}Homebrew LLVM${ColorReset} library paths and -lunwind (not in environment)")
                else()
                    message(STATUS "${BoldYellow}Homebrew LLVM${ColorReset} library paths already present in linker flags")
                endif()
            else()
                message(STATUS "${BoldBlue}Homebrew LLVM${ColorReset} library paths ${BoldGreen}already present${ColorReset} in LDFLAGS environment variable")
            endif()

            message(STATUS "${BoldGreen}Applied${ColorReset} ${BoldBlue}Homebrew LLVM${ColorReset} toolchain flags:")
            message(STATUS "  Include: (using compiler's resource directory - NOT added globally)")
            if(NOT DEFINED ENV{LDFLAGS} OR NOT "$ENV{LDFLAGS}" MATCHES "-L.*llvm")
                message(STATUS "  Link: ${BoldCyan}${HOMEBREW_LLVM_LINK_FLAGS}${ColorReset}")
            else()
                message(STATUS "  Link: from ${BoldCyan}LDFLAGS${ColorReset}=${BoldYellow}$ENV{LDFLAGS}${ColorReset} environment variable")
            endif()
        endif()
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
    # Use llvm-config to find the correct LLVM installation
    find_program(LLVM_CONFIG
        NAMES llvm-config llvm-config-21 llvm-config-20 llvm-config-19 llvm-config-18 llvm-config-17 llvm-config-16 llvm-config-15
        DOC "Path to llvm-config"
        PATHS ${LLVM_ROOT_DIRS}
    )

    if(LLVM_CONFIG)
        # Get LLVM binary directory from llvm-config
        execute_process(
            COMMAND ${LLVM_CONFIG} --bindir
            OUTPUT_VARIABLE LLVM_BINDIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        # Find llvm-ar and llvm-ranlib in LLVM's binary directory
        find_program(LLVM_AR
            NAMES llvm-ar
            PATHS ${LLVM_BINDIR}
        )
        find_program(LLVM_RANLIB
            NAMES llvm-ranlib
            PATHS ${LLVM_BINDIR}
        )
    else()
        # Fallback: search in common LLVM installation directories
        find_program(LLVM_AR
            NAMES llvm-ar llvm-ar-21 llvm-ar-20 llvm-ar-19 llvm-ar-18 llvm-ar-17 llvm-ar-16 llvm-ar-15
            PATHS ${LLVM_ROOT_DIRS}
        )
        find_program(LLVM_RANLIB
            NAMES llvm-ranlib llvm-ranlib-21 llvm-ranlib-20 llvm-ranlib-19 llvm-ranlib-18 llvm-ranlib-17 llvm-ranlib-16 llvm-ranlib-15
            PATHS ${LLVM_ROOT_DIRS}
        )
    endif()

    if(LLVM_AR)
        set(CMAKE_AR "${LLVM_AR}" CACHE FILEPATH "Archiver" FORCE)
    endif()
    if(LLVM_RANLIB)
        set(CMAKE_RANLIB "${LLVM_RANLIB}" CACHE FILEPATH "Ranlib" FORCE)
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

