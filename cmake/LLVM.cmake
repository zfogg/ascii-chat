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

function(configure_llvm_pre_project)
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

    if(HOMEBREW_LLVM_PREFIX)
        message(STATUS "Found Homebrew LLVM at: ${HOMEBREW_LLVM_PREFIX}")
        set(USE_HOMEBREW_LLVM TRUE)

        if(USE_HOMEBREW_LLVM)
            # Check if compiler is already set to Homebrew LLVM
            if(CMAKE_C_COMPILER MATCHES "${HOMEBREW_LLVM_PREFIX}")
                message(STATUS "Using user-specified Homebrew LLVM compiler: ${CMAKE_C_COMPILER}")
            else()
                message(STATUS "Configuring to use Homebrew LLVM")

                # Check for ccache and use it if available
                find_program(CCACHE_PROGRAM ccache)
                if(CCACHE_PROGRAM)
                    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
                    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
                    # Set the actual compilers (ccache will wrap them via the launcher)
                    set(CMAKE_C_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang" CACHE FILEPATH "C compiler" FORCE)
                    message(STATUS "Using ccache with Homebrew LLVM")
                else()
                    # No ccache, use LLVM directly
                    set(CMAKE_C_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang" CACHE FILEPATH "C compiler" FORCE)
                endif()
            endif()

            # Add LLVM bin directory to PATH for tools like llvm-ar, llvm-ranlib, etc.
            set(CMAKE_PREFIX_PATH "${HOMEBREW_LLVM_PREFIX}" ${CMAKE_PREFIX_PATH} PARENT_SCOPE)

            # Add LLVM CMake modules to module path for advanced features
            if(EXISTS "${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm")
                list(APPEND CMAKE_MODULE_PATH "${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm")
                message(STATUS "Added LLVM CMake modules: ${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm")
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
                ERROR_QUIET
            )
            string(REGEX MATCH "clang version ([0-9]+)\\.([0-9]+)" CLANG_VERSION_MATCH "${CLANG_VERSION_OUTPUT}")
            set(CLANG_MAJOR_VERSION "${CMAKE_MATCH_1}")

            # Construct resource directory (don't trust -print-resource-dir, it returns wrong path)
            set(CLANG_RESOURCE_DIR "${HOMEBREW_LLVM_PREFIX}/lib/clang/${CLANG_MAJOR_VERSION}")

            if(EXISTS "${CLANG_RESOURCE_DIR}/include")
                message(STATUS "Found Clang resource directory: ${CLANG_RESOURCE_DIR}")
                # Append to CMAKE_*_FLAGS so it takes effect for project() and all subdirectories (including mimalloc)
                string(APPEND CMAKE_C_FLAGS " -resource-dir ${CLANG_RESOURCE_DIR}")
                string(APPEND CMAKE_CXX_FLAGS " -resource-dir ${CLANG_RESOURCE_DIR}")
                string(APPEND CMAKE_OBJC_FLAGS " -resource-dir ${CLANG_RESOURCE_DIR}")
                # Export to parent scope
                set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
                set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS}" PARENT_SCOPE)
            else()
                message(WARNING "Could not find Clang resource directory at: ${CLANG_RESOURCE_DIR}")
            endif()

            # Get macOS SDK path for standard headers
            execute_process(
                COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE MACOS_SDK_PATH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )

            if(MACOS_SDK_PATH)
                message(STATUS "Found macOS SDK at: ${MACOS_SDK_PATH}")
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

            message(STATUS "Configured Homebrew LLVM: ${CMAKE_C_COMPILER}")
        endif()
    else()
        message(STATUS "Homebrew LLVM not found, using system compiler")
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
            # Store the flags but don't add -lunwind globally to avoid duplication
            set(HOMEBREW_LLVM_LINK_DIRS "-L${COMPILER_PREFIX}/lib -L${COMPILER_PREFIX}/lib/c++ -L${COMPILER_PREFIX}/lib/unwind")

            # Use full path to libunwind instead of -lunwind to avoid macOS linker search issues
            set(HOMEBREW_LLVM_LIBS "${COMPILER_PREFIX}/lib/unwind/libunwind.a")

            # Export to parent scope AND cache for later use
            set(HOMEBREW_LLVM_LIBS "${HOMEBREW_LLVM_LIBS}" CACHE INTERNAL "Homebrew LLVM libraries")
            set(HOMEBREW_LLVM_LIB_DIR "${COMPILER_PREFIX}/lib" CACHE INTERNAL "Homebrew LLVM library directory")

            if(NOT DEFINED ENV{LDFLAGS} OR NOT "$ENV{LDFLAGS}" MATCHES "-L.*llvm")
                # Add only the library search paths globally (not the actual libraries)
                # Check if paths are already present to avoid duplication
                if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "-L.*llvm/lib/unwind")
                    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${HOMEBREW_LLVM_LINK_DIRS}" CACHE STRING "Linker flags" FORCE)
                    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${HOMEBREW_LLVM_LINK_DIRS}" CACHE STRING "Shared linker flags" FORCE)
                    message(STATUS "Added Homebrew LLVM library paths (not in environment)")
                else()
                    message(STATUS "Homebrew LLVM library paths already present in linker flags")
                endif()
            else()
                message(STATUS "Skipping Homebrew LLVM library paths (already in LDFLAGS environment variable)")
            endif()

            message(STATUS "Applied Homebrew LLVM toolchain flags:")
            message(STATUS "  Include: (using compiler's resource directory - NOT added globally)")
            if(NOT DEFINED ENV{LDFLAGS} OR NOT "$ENV{LDFLAGS}" MATCHES "-L.*llvm")
                message(STATUS "  Link: ${HOMEBREW_LLVM_LINK_DIRS}")
            else()
                message(STATUS "  Link: (from LDFLAGS environment variable)")
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
        NAMES llvm-config-20 llvm-config-19 llvm-config-18 llvm-config
        DOC "Path to llvm-config"
    )

    if(LLVM_CONFIG)
        # Get LLVM binary directory from llvm-config
        execute_process(
            COMMAND ${LLVM_CONFIG} --bindir
            OUTPUT_VARIABLE LLVM_BINDIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        message(STATUS "Found LLVM via llvm-config: ${LLVM_CONFIG}")
        message(STATUS "LLVM binary directory: ${LLVM_BINDIR}")

        # Find llvm-ar and llvm-ranlib in LLVM's binary directory
        find_program(LLVM_AR
            NAMES llvm-ar
            PATHS ${LLVM_BINDIR}
            NO_DEFAULT_PATH
        )
        find_program(LLVM_RANLIB
            NAMES llvm-ranlib
            PATHS ${LLVM_BINDIR}
            NO_DEFAULT_PATH
        )
    else()
        # Fallback: search in common LLVM installation directories
        message(STATUS "llvm-config not found, searching common paths")
        find_program(LLVM_AR
            NAMES llvm-ar
            PATHS /usr/lib/llvm-20/bin /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin
            NO_DEFAULT_PATH
        )
        find_program(LLVM_RANLIB
            NAMES llvm-ranlib
            PATHS /usr/lib/llvm-20/bin /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin
            NO_DEFAULT_PATH
        )
    endif()

    if(LLVM_AR)
        set(CMAKE_AR "${LLVM_AR}" CACHE FILEPATH "Archiver" FORCE)
        message(STATUS "Using LLVM archiver: ${LLVM_AR}")
    endif()
    if(LLVM_RANLIB)
        set(CMAKE_RANLIB "${LLVM_RANLIB}" CACHE FILEPATH "Ranlib" FORCE)
        message(STATUS "Using LLVM ranlib: ${LLVM_RANLIB}")
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

