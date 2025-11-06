# =============================================================================
# Compiler Detection Module
# =============================================================================
# This module handles compiler detection, environment variables, and
# compile_commands.json setup
#
# Prerequisites:
#   - Must run after project()
#
# Outputs:
#   - CMAKE_C_COMPILER configured
#   - CMAKE_C_FLAGS from environment variables
#   - compile_commands.json symlink/copy in project root
# =============================================================================

# 1. First priority: Respect CC environment variables if set
if(DEFINED ENV{CC} AND NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "$ENV{CC}" CACHE FILEPATH "C compiler from environment")
    message(STATUS "Using C compiler from CC environment variable: $ENV{CC}")
endif()

# 2. Respect CFLAGS, CPPFLAGS, LDFLAGS from environment
if(DEFINED ENV{CFLAGS})
    set(CMAKE_C_FLAGS "$ENV{CFLAGS} ${CMAKE_C_FLAGS}" CACHE STRING "C compiler flags")
    message(STATUS "Using CFLAGS from environment: $ENV{CFLAGS}")
endif()

if(DEFINED ENV{CPPFLAGS})
    # CPPFLAGS should be added to both C flags for preprocessor directives
    set(CMAKE_C_FLAGS "$ENV{CPPFLAGS} ${CMAKE_C_FLAGS}" CACHE STRING "C compiler flags")
    message(STATUS "Using CPPFLAGS from environment: $ENV{CPPFLAGS}")
endif()

if(DEFINED ENV{LDFLAGS})
    set(CMAKE_EXE_LINKER_FLAGS "$ENV{LDFLAGS} ${CMAKE_EXE_LINKER_FLAGS}" CACHE STRING "Linker flags")
    set(CMAKE_SHARED_LINKER_FLAGS "$ENV{LDFLAGS} ${CMAKE_SHARED_LINKER_FLAGS}" CACHE STRING "Shared linker flags")
    message(STATUS "Using LDFLAGS from environment: $ENV{LDFLAGS}")
endif()

# 3. Set default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build" FORCE)
    message(STATUS "Set default build type to ${BoldYellow}Debug${ColorReset}")
endif()

# 4. Handle ccache wrapper if present
if(CMAKE_C_COMPILER)
    # Check if compiler is a ccache wrapper
    if(CMAKE_C_COMPILER MATCHES "ccache")
        # Try to find the real compiler
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-prog-name=clang
            OUTPUT_VARIABLE REAL_C_COMPILER
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(REAL_C_COMPILER AND NOT REAL_C_COMPILER STREQUAL "clang")
            message(STATUS "Detected ccache wrapper, real compiler: ${REAL_C_COMPILER}")
            # Don't override CMAKE_C_COMPILER but note for sanitizer setup
            set(CCACHE_DETECTED TRUE)
        endif()
    endif()
endif()

# Generate compile_commands.json for IDE/tool integration (clangd, VSCode, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Automatically copy or symlink compile_commands.json to project root
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    if(NOT CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
        # Only do this if we're doing an out-of-source build
        set(COMPILE_COMMANDS_SOURCE "${CMAKE_BINARY_DIR}/compile_commands.json")
        set(COMPILE_COMMANDS_DEST "${CMAKE_SOURCE_DIR}/compile_commands.json")

        if(WIN32)
            # On Windows, create a junction/symlink at configure time
            # This way the file will be available as soon as Ninja generates it
            # Remove existing file/link first
            if(EXISTS "${COMPILE_COMMANDS_DEST}")
                file(REMOVE "${COMPILE_COMMANDS_DEST}")
            endif()

            # Try to create a symlink (requires developer mode or admin on Windows)
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E create_symlink
                    "${COMPILE_COMMANDS_SOURCE}"
                    "${COMPILE_COMMANDS_DEST}"
                RESULT_VARIABLE symlink_result
                ERROR_QUIET
            )

            if(symlink_result EQUAL 0)
                message(STATUS "Created symlink for ${BoldCyan}compile_commands.json${ColorReset} in project root")
            else()
                # Symlink failed, we'll copy it as a custom target instead
                message(STATUS "Will copy compile_commands.json to project root after build")
                add_custom_target(copy_compile_commands ALL
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${COMPILE_COMMANDS_SOURCE}"
                        "${COMPILE_COMMANDS_DEST}"
                    COMMENT "Copying compile_commands.json to project root"
                    VERBATIM
                )
            endif()
        else()
            # On Unix-like systems, symlinks work reliably
            if(EXISTS "${COMPILE_COMMANDS_DEST}")
                file(REMOVE "${COMPILE_COMMANDS_DEST}")
            endif()

            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E create_symlink
                    "${COMPILE_COMMANDS_SOURCE}"
                    "${COMPILE_COMMANDS_DEST}"
                RESULT_VARIABLE symlink_result
            )

            if(symlink_result EQUAL 0)
                message(STATUS "Created symlink for ${BoldCyan}compile_commands.json${ColorReset} in project root")
            endif()
        endif()
    endif()
endif()

# Clear Windows-specific CMake flags for native Clang builds
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    # CMake automatically adds MSVC runtime flags even for Clang, clear them
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreaded "")
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDLL "")
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebug "")
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebugDLL "")

    # Clear the runtime library setting that adds -D_DEBUG -D_DLL -D_MT
    set(CMAKE_MSVC_RUNTIME_LIBRARY "")

    message(STATUS "Cleared CMake Windows runtime flags for ${BoldGreen}Clang-only${ColorReset} build")
endif()

# =============================================================================
# Warn if MSVC is detected (but allow build to continue)
# =============================================================================
# This check happens after project() has determined the compiler ID
if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
    message(WARNING "
╔═══════════════════════════════════════════════════════════════════════════╗
║                          ⚠️  COMPILER WARNING ⚠️                          ║
╠═══════════════════════════════════════════════════════════════════════════╣
║                                                                           ║
║  MSVC detected. This project is designed for and tested with Clang/GCC.  ║
║                                                                           ║
║  MSVC is NOT officially supported and may cause build failures.           ║
║                                                                           ║
║  Recommended: Install Clang and use:                                      ║
║    - Windows: scoop install llvm                                          ║
║    - Or use: cmake -DCMAKE_C_COMPILER=clang -B build                      ║
║                                                                           ║
║  The build will continue but may fail or produce unexpected results.     ║
║                                                                           ║
╚═══════════════════════════════════════════════════════════════════════════╝
")
endif()

