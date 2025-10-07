# =============================================================================
# HomebrewLLVM.cmake - Homebrew LLVM toolchain configuration for macOS
# =============================================================================
# Auto-detects and configures Homebrew LLVM (if installed) as the compiler
# toolchain on macOS, providing full LLVM/Clang features and tools.
#
# This module handles:
# - Compiler detection and selection
# - Clang resource directory configuration
# - macOS SDK path configuration
# - LLVM library paths and linking
# - LLVM tools (llvm-ar, llvm-ranlib, etc.)
#
# Must be included BEFORE project() to set compiler variables.
# =============================================================================

# =============================================================================
# Part 1: Pre-project() Configuration (Compiler Selection)
# =============================================================================
# This section must run before project() to set CMAKE_C_COMPILER
# =============================================================================

function(configure_homebrew_llvm_pre_project)
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

function(configure_homebrew_llvm_post_project)
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
