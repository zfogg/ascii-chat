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
    else()
        if(DEFINED CMAKE_C_COMPILER_LAUNCHER AND CMAKE_C_COMPILER_LAUNCHER MATCHES "ccache")
            message(STATUS "${BoldYellow}ccache${ColorReset} not found; clearing compiler launcher.")
            unset(CMAKE_C_COMPILER_LAUNCHER CACHE)
        endif()
    endif()

    # Use llvm-config to detect LLVM installation (if available in PATH)
    find_program(LLVM_CONFIG_EXECUTABLE
        NAMES llvm-config llvm-config.exe
        DOC "Path to llvm-config"
    )

    if(LLVM_CONFIG_EXECUTABLE)
        # Get LLVM installation prefix
        execute_process(
            COMMAND ${LLVM_CONFIG_EXECUTABLE} --prefix
            OUTPUT_VARIABLE LLVM_DETECTED_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        # Get LLVM CMake directory
        execute_process(
            COMMAND ${LLVM_CONFIG_EXECUTABLE} --cmakedir
            OUTPUT_VARIABLE LLVM_DETECTED_CMAKEDIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        if(LLVM_DETECTED_PREFIX AND LLVM_DETECTED_CMAKEDIR)
            message(STATUS "${BoldGreen}Detected${ColorReset} ${BoldBlue}LLVM${ColorReset} via llvm-config: ${BoldCyan}${LLVM_DETECTED_PREFIX}${ColorReset}")
            message(STATUS "${BoldGreen}Detected${ColorReset} ${BoldBlue}LLVM CMake${ColorReset} directory: ${BoldCyan}${LLVM_DETECTED_CMAKEDIR}${ColorReset}")

            # Add LLVM prefix and cmake directory to CMAKE_PREFIX_PATH for find_package()
            list(APPEND CMAKE_PREFIX_PATH "${LLVM_DETECTED_PREFIX}")
            list(APPEND CMAKE_PREFIX_PATH "${LLVM_DETECTED_CMAKEDIR}")

            # Export to parent scope for find_package(LLVM) and find_package(Clang)
            set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)

            # Add Clang cmake directory as well
            if(EXISTS "${LLVM_DETECTED_PREFIX}/lib/cmake/clang")
                list(APPEND CMAKE_PREFIX_PATH "${LLVM_DETECTED_PREFIX}/lib/cmake/clang")
                set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
                message(STATUS "${BoldGreen}Detected${ColorReset} ${BoldBlue}Clang CMake${ColorReset} directory: ${BoldCyan}${LLVM_DETECTED_PREFIX}/lib/cmake/clang${ColorReset}")
            endif()
        endif()
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

    # Check if LLVM is installed (either via llvm-config or Homebrew)
    set(HOMEBREW_LLVM_PREFIX "")
    set(LLVM_SOURCE "")  # Track whether LLVM is from llvm-config or Homebrew

    # First priority: If llvm-config was detected and points to a valid LLVM installation,
    # use that instead of Homebrew paths. This allows git-built LLVM to take precedence.
    if(LLVM_DETECTED_PREFIX AND EXISTS "${LLVM_DETECTED_PREFIX}/bin/clang")
        set(HOMEBREW_LLVM_PREFIX "${LLVM_DETECTED_PREFIX}")
        set(LLVM_SOURCE "llvm-config")
        message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}LLVM${ColorReset} (from llvm-config): ${BoldCyan}${LLVM_DETECTED_PREFIX}${ColorReset}")
    # Second priority: Check common Homebrew installation paths
    elseif(EXISTS "/usr/local/opt/llvm/bin/clang")
        set(HOMEBREW_LLVM_PREFIX "/usr/local/opt/llvm")
        set(LLVM_SOURCE "Homebrew")
    elseif(EXISTS "/opt/homebrew/opt/llvm/bin/clang")
        set(HOMEBREW_LLVM_PREFIX "/opt/homebrew/opt/llvm")
        set(LLVM_SOURCE "Homebrew")
    endif()

    # Set the compiler (all platforms)
    set(CMAKE_C_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang" CACHE FILEPATH "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang++" CACHE FILEPATH "CXX compiler" FORCE)
    set(CMAKE_OBJC_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang" CACHE FILEPATH "Objective-C compiler" FORCE)


    if(HOMEBREW_LLVM_PREFIX)
        # Display appropriate message based on LLVM source
        if(LLVM_SOURCE STREQUAL "llvm-config")
            message(STATUS "${BoldGreen}Found${ColorReset} ${BoldBlue}LLVM${ColorReset} (from llvm-config) at: ${BoldCyan}${HOMEBREW_LLVM_PREFIX}${ColorReset}")
        else()
            message(STATUS "${BoldGreen}Found${ColorReset} ${BoldBlue}Homebrew LLVM${ColorReset} at: ${BoldCyan}${HOMEBREW_LLVM_PREFIX}${ColorReset}")
        endif()
        set(USE_HOMEBREW_LLVM TRUE)

        if(USE_HOMEBREW_LLVM)
            # Check if compiler is already set
            if(CMAKE_C_COMPILER MATCHES "${HOMEBREW_LLVM_PREFIX}")
                if(LLVM_SOURCE STREQUAL "llvm-config")
                    message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}LLVM${ColorReset} (from llvm-config) compiler: ${BoldCyan}${CMAKE_C_COMPILER}${ColorReset}")
                else()
                    message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}Homebrew LLVM${ColorReset} compiler: ${BoldCyan}${CMAKE_C_COMPILER}${ColorReset}")
                endif()
            else()
                if(LLVM_SOURCE STREQUAL "llvm-config")
                    message(STATUS "Configuring to use ${BoldBlue}LLVM${ColorReset} (from llvm-config)")
                else()
                    message(STATUS "Configuring to use ${BoldBlue}Homebrew LLVM${ColorReset}")
                endif()
            endif()

            # Add LLVM bin directory to PATH for tools like llvm-ar, llvm-ranlib, etc.
            set(CMAKE_PREFIX_PATH "${HOMEBREW_LLVM_PREFIX}" ${CMAKE_PREFIX_PATH} PARENT_SCOPE)

            # Add LLVM CMake modules to module path for advanced features
            if(EXISTS "${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm")
                list(APPEND CMAKE_MODULE_PATH "${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm")
                message(STATUS "${BoldGreen}Added${ColorReset} ${BoldBlue}LLVM CMake${ColorReset} modules:${ColorReset} ${BoldCyan}${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm${ColorReset}")
            endif()

            # Set LLVM tool paths
            set(CMAKE_AR "${HOMEBREW_LLVM_PREFIX}/bin/llvm-ar" CACHE FILEPATH "Archiver" FORCE)
            set(CMAKE_RANLIB "${HOMEBREW_LLVM_PREFIX}/bin/llvm-ranlib" CACHE FILEPATH "Ranlib" FORCE)

            # Configure resource directory BEFORE project() so flags are set early
            # Use -print-resource-dir to get the actual resource directory
            execute_process(
                COMMAND "${HOMEBREW_LLVM_PREFIX}/bin/clang" -print-resource-dir
                OUTPUT_VARIABLE CLANG_RESOURCE_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )

            # Fallback: construct from version if -print-resource-dir failed
            if(NOT CLANG_RESOURCE_DIR OR NOT EXISTS "${CLANG_RESOURCE_DIR}/include")
                execute_process(
                    COMMAND "${HOMEBREW_LLVM_PREFIX}/bin/clang" --version
                    OUTPUT_VARIABLE CLANG_VERSION_OUTPUT
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                string(REGEX MATCH "clang version ([0-9]+)\\.([0-9]+)" CLANG_VERSION_MATCH "${CLANG_VERSION_OUTPUT}")
                set(CLANG_MAJOR_VERSION "${CMAKE_MATCH_1}")
                set(CLANG_RESOURCE_DIR "${HOMEBREW_LLVM_PREFIX}/lib/clang/${CLANG_MAJOR_VERSION}")
            endif()

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

            if(LLVM_SOURCE STREQUAL "llvm-config")
                message(STATUS "${BoldGreen}Configured${ColorReset} ${BoldBlue}LLVM${ColorReset} (from llvm-config): ${BoldCyan}${CMAKE_C_COMPILER}${ColorReset}")
            else()
                message(STATUS "${BoldGreen}Configured${ColorReset} ${BoldBlue}Homebrew LLVM${ColorReset}: ${BoldCyan}${CMAKE_C_COMPILER}${ColorReset}")
            endif()
        endif()
    else()
        message(STATUS "${BoldBlue}LLVM${ColorReset} not found (neither llvm-config nor Homebrew), using system compiler")
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

    # Check if we're using LLVM (either from llvm-config, Homebrew, or user-specified)
    if(CMAKE_C_COMPILER)
        get_filename_component(COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(COMPILER_PREFIX "${COMPILER_DIR}" DIRECTORY)

        # Detect LLVM source (llvm-config vs Homebrew)
        set(IS_HOMEBREW_LLVM FALSE)
        set(LLVM_SOURCE_NAME "LLVM")

        # Check for Homebrew LLVM installations first (more specific paths)
        if(COMPILER_PREFIX MATCHES "/opt/homebrew/opt/llvm" OR COMPILER_PREFIX MATCHES "/usr/local/opt/llvm")
            set(IS_HOMEBREW_LLVM TRUE)
            set(LLVM_SOURCE_NAME "Homebrew LLVM")
        # Check for ccache wrappers
        elseif(COMPILER_PREFIX MATCHES "ccache")
            if(EXISTS "/usr/local/opt/llvm/bin/clang")
                set(COMPILER_PREFIX "/usr/local/opt/llvm")
                set(IS_HOMEBREW_LLVM TRUE)
                set(LLVM_SOURCE_NAME "Homebrew LLVM")
            elseif(EXISTS "/opt/homebrew/opt/llvm/bin/clang")
                set(COMPILER_PREFIX "/opt/homebrew/opt/llvm")
                set(IS_HOMEBREW_LLVM TRUE)
                set(LLVM_SOURCE_NAME "Homebrew LLVM")
            elseif(EXISTS "/usr/local/bin/llvm-config")
                set(COMPILER_PREFIX "/usr/local")
                set(IS_HOMEBREW_LLVM TRUE)
                set(LLVM_SOURCE_NAME "LLVM (from llvm-config)")
            endif()
        # Check for git-built LLVM at /usr/local (no ccache)
        # Also check /opt/homebrew for Apple Silicon git-built LLVM
        elseif((COMPILER_PREFIX STREQUAL "/usr/local" OR COMPILER_PREFIX STREQUAL "/opt/homebrew") AND EXISTS "${COMPILER_PREFIX}/bin/llvm-config")
            # Check if compiler is actually from Homebrew path or from /usr/local
            # If CMAKE_C_COMPILER is /usr/local/bin/clang or /opt/homebrew/bin/clang, it's git-built
            # If it's /usr/local/opt/llvm/bin/clang, it's Homebrew
            if(CMAKE_C_COMPILER MATCHES "/opt/llvm/" OR CMAKE_C_COMPILER MATCHES "Cellar/llvm/")
                # Compiler is from Homebrew, determine which prefix
                if(EXISTS "/usr/local/opt/llvm/bin/clang")
                    set(COMPILER_PREFIX "/usr/local/opt/llvm")
                else()
                    set(COMPILER_PREFIX "/opt/homebrew/opt/llvm")
                endif()
                set(IS_HOMEBREW_LLVM TRUE)
                set(LLVM_SOURCE_NAME "Homebrew LLVM")
            else()
                # Compiler is from /usr/local/bin or /opt/homebrew/bin directly (git-built)
                set(IS_HOMEBREW_LLVM TRUE)
                set(LLVM_SOURCE_NAME "LLVM (from llvm-config)")
            endif()
        endif()

        if(IS_HOMEBREW_LLVM)
            # Add the full LDFLAGS as recommended by brew info llvm
            # This includes libc++, libunwind which are needed for full LLVM toolchain
            # For Release builds: Link libunwind statically
            # For Debug/Dev builds: Link dynamically for faster iteration
            if(CMAKE_BUILD_TYPE STREQUAL "Release")
                # Use absolute path to static libunwind.a for Release builds
                # Check both Homebrew layout (lib/unwind/) and git-built layout (lib/)
                if(EXISTS "${COMPILER_PREFIX}/lib/unwind/libunwind.a")
                    set(ASCIICHAT_LLVM_STATIC_LIBUNWIND "${COMPILER_PREFIX}/lib/unwind/libunwind.a")
                    message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}static libunwind${ColorReset} (Homebrew): ${ASCIICHAT_LLVM_STATIC_LIBUNWIND}")
                elseif(EXISTS "${COMPILER_PREFIX}/lib/libunwind.a")
                    set(ASCIICHAT_LLVM_STATIC_LIBUNWIND "${COMPILER_PREFIX}/lib/libunwind.a")
                    message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}static libunwind${ColorReset} (git-built): ${ASCIICHAT_LLVM_STATIC_LIBUNWIND}")
                else()
                    message(FATAL_ERROR "Could not find static ${BoldRed}libunwind.a${ColorReset} in ${COMPILER_PREFIX}/lib/unwind/ or ${COMPILER_PREFIX}/lib/")
                endif()
                set(HOMEBREW_LLVM_LINK_FLAGS "")

                foreach(_llvm_flag_var CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS)
                    set(_current_flags "${${_llvm_flag_var}}")
                    if(NOT _current_flags)
                        continue()
                    endif()

                    # Split current flags respecting shell quoting (macOS toolchain flags use commas)
                    separate_arguments(_llvm_flags_list NATIVE_COMMAND "${_current_flags}")

                    set(_filtered_flags "")
                    set(_added_static_lib FALSE)
                    foreach(_flag IN LISTS _llvm_flags_list)
                        if(_flag MATCHES "^-L${COMPILER_PREFIX}/lib/(unwind|c\\+\\+)$")
                            continue()
                        elseif(_flag STREQUAL "-lunwind")
                            continue()
                        elseif(_flag STREQUAL "${ASCIICHAT_LLVM_STATIC_LIBUNWIND}")
                            if(_added_static_lib)
                                continue()
                            endif()
                            set(_added_static_lib TRUE)
                        endif()
                        list(APPEND _filtered_flags "${_flag}")
                    endforeach()

                    if(_filtered_flags)
                        list(REMOVE_DUPLICATES _filtered_flags)
                        string(REPLACE ";" " " _filtered_flags "${_filtered_flags}")
                        set(${_llvm_flag_var} "${_filtered_flags}" CACHE STRING "Linker flags" FORCE)
                    else()
                        set(${_llvm_flag_var} "" CACHE STRING "Linker flags" FORCE)
                    endif()
                endforeach()
            else()
                # Debug/Dev builds: Use dynamic linking for faster development
                # Check library layout: Homebrew uses subdirectories, git-built uses flat lib/
                set(HOMEBREW_LLVM_LINK_DIRS "")
                if(EXISTS "${COMPILER_PREFIX}/lib/unwind" AND EXISTS "${COMPILER_PREFIX}/lib/c++")
                    # Homebrew layout: lib/unwind/ and lib/c++/
                    set(HOMEBREW_LLVM_LINK_DIRS "-L${COMPILER_PREFIX}/lib/unwind -L${COMPILER_PREFIX}/lib/c++")
                    message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}Homebrew LLVM${ColorReset} library layout")
                elseif(EXISTS "${COMPILER_PREFIX}/lib/libunwind.a" OR EXISTS "${COMPILER_PREFIX}/lib/libunwind.dylib")
                    # Git-built layout: lib/ (flat)
                    set(HOMEBREW_LLVM_LINK_DIRS "-L${COMPILER_PREFIX}/lib")
                    message(STATUS "${BoldGreen}Using${ColorReset} ${BoldBlue}git-built LLVM${ColorReset} library layout")
                else()
                    message(WARNING "Could not detect LLVM library layout, defaulting to ${COMPILER_PREFIX}/lib")
                    set(HOMEBREW_LLVM_LINK_DIRS "-L${COMPILER_PREFIX}/lib")
                endif()
                set(HOMEBREW_LLVM_LINK_FLAGS "${HOMEBREW_LLVM_LINK_DIRS} -lunwind")
            endif()

            # Export to cache for later use
            set(HOMEBREW_LLVM_LIB_DIR "${COMPILER_PREFIX}/lib" CACHE INTERNAL "Homebrew LLVM library directory")

            if(HOMEBREW_LLVM_LINK_FLAGS AND (NOT DEFINED ENV{LDFLAGS} OR NOT "$ENV{LDFLAGS}" MATCHES "-L.*llvm"))
                # Add library search paths and -lunwind flag globally
                # Check if paths are already present to avoid duplication
                if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "-L.*llvm/lib/unwind")
                    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${HOMEBREW_LLVM_LINK_FLAGS}" CACHE STRING "Linker flags" FORCE)
                    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${HOMEBREW_LLVM_LINK_FLAGS}" CACHE STRING "Shared linker flags" FORCE)
                    message(STATUS "${BoldGreen}Added${ColorReset} ${BoldBlue}${LLVM_SOURCE_NAME}${ColorReset} library paths and -lunwind (not in environment)")
                else()
                    message(STATUS "${BoldYellow}${LLVM_SOURCE_NAME}${ColorReset} library paths already present in linker flags")
                endif()
            else()
                message(STATUS "${BoldBlue}${LLVM_SOURCE_NAME}${ColorReset} library paths ${BoldGreen}already present${ColorReset} in LDFLAGS environment variable")
            endif()

            # Add LLVM library paths to rpath for Debug/Dev builds (for dynamic linking)
            # This ensures libunwind.dylib and other LLVM libraries can be found at runtime
            if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
                # Determine the correct library path based on layout
                if(EXISTS "${COMPILER_PREFIX}/lib/unwind" AND EXISTS "${COMPILER_PREFIX}/lib/c++")
                    # Homebrew layout: separate subdirectories
                    set(LLVM_RPATH_UNWIND "${COMPILER_PREFIX}/lib/unwind")
                    set(LLVM_RPATH_CXX "${COMPILER_PREFIX}/lib/c++")
                elseif(EXISTS "${COMPILER_PREFIX}/lib/libunwind.dylib")
                    # Git-built layout: flat lib directory
                    set(LLVM_RPATH_UNWIND "${COMPILER_PREFIX}/lib")
                    set(LLVM_RPATH_CXX "${COMPILER_PREFIX}/lib")
                endif()

                # Add to build rpath if directories exist
                if(LLVM_RPATH_UNWIND)
                    if(NOT CMAKE_BUILD_RPATH MATCHES "${LLVM_RPATH_UNWIND}")
                        list(APPEND CMAKE_BUILD_RPATH "${LLVM_RPATH_UNWIND}")
                        set(CMAKE_BUILD_RPATH "${CMAKE_BUILD_RPATH}" CACHE INTERNAL "Build RPATH" FORCE)
                        message(STATUS "${BoldGreen}Added${ColorReset} ${BoldBlue}LLVM unwind${ColorReset} to build rpath: ${BoldCyan}${LLVM_RPATH_UNWIND}${ColorReset}")
                    endif()
                endif()

                if(LLVM_RPATH_CXX AND NOT LLVM_RPATH_CXX STREQUAL LLVM_RPATH_UNWIND)
                    if(NOT CMAKE_BUILD_RPATH MATCHES "${LLVM_RPATH_CXX}")
                        list(APPEND CMAKE_BUILD_RPATH "${LLVM_RPATH_CXX}")
                        set(CMAKE_BUILD_RPATH "${CMAKE_BUILD_RPATH}" CACHE INTERNAL "Build RPATH" FORCE)
                        message(STATUS "${BoldGreen}Added${ColorReset} ${BoldBlue}LLVM c++${ColorReset} to build rpath: ${BoldCyan}${LLVM_RPATH_CXX}${ColorReset}")
                    endif()
                endif()
            endif()

            message(STATUS "${BoldGreen}Applied${ColorReset} ${BoldBlue}${LLVM_SOURCE_NAME}${ColorReset} toolchain flags:")
            message(STATUS "  Include: (using compiler's resource directory - NOT added globally)")
            if(HOMEBREW_LLVM_LINK_FLAGS AND (NOT DEFINED ENV{LDFLAGS} OR NOT "$ENV{LDFLAGS}" MATCHES "-L.*llvm"))
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

