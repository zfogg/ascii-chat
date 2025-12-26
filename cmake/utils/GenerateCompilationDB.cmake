# =============================================================================
# Compilation Database Generation Utility
# =============================================================================
# Provides a reusable function for generating temporary compilation databases
# for source transformation tools (defer, panic, etc.).
#
# Usage:
#   generate_compilation_database(
#       OUTPUT <output_file>                # e.g., "${CMAKE_BINARY_DIR}/compile_commands_defer.json"
#       TEMP_DIR <temp_dir>                 # e.g., "${CMAKE_BINARY_DIR}/compile_db_temp_defer"
#       LOG_FILE <log_file>                 # e.g., "${CMAKE_BINARY_DIR}/defer_compile_db.log"
#       COMMENT <comment>                   # Comment for the custom command
#       [CLANG_RESOURCE_DIR <dir>]          # Optional: Clang resource directory for -resource-dir flag
#       [DISABLE_OPTIONS <opt1> <opt2>...]  # Options to disable (e.g., "ASCIICHAT_BUILD_WITH_DEFER")
#   )
#
# The function creates a custom command that:
#   1. Removes and recreates the temp directory
#   2. Runs cmake configure with compile commands export
#   3. Builds the generate_version target
#   4. Copies compile_commands.json to the output location
#
# Platform handling:
#   - Windows: Uses cmd /c with stderr/stdout redirection
#   - Unix: Uses sh -c with shell redirection
#
# Example:
#   generate_compilation_database(
#       OUTPUT "${CMAKE_BINARY_DIR}/compile_commands_defer.json"
#       TEMP_DIR "${CMAKE_BINARY_DIR}/compile_db_temp_defer"
#       LOG_FILE "${CMAKE_BINARY_DIR}/defer_compile_db.log"
#       COMMENT "Generating compilation database for defer transformation tool"
#       DISABLE_OPTIONS ASCIICHAT_BUILD_WITH_DEFER ASCIICHAT_USE_PCH ASCIICHAT_ENABLE_ANALYZERS
#   )
# =============================================================================

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_GENERATE_COMPILATION_DB_INCLUDED)
    return()
endif()
set(_ASCIICHAT_GENERATE_COMPILATION_DB_INCLUDED TRUE)

function(generate_compilation_database)
    # Parse arguments
    set(_options)
    set(_one_value_args OUTPUT TEMP_DIR LOG_FILE COMMENT CLANG_RESOURCE_DIR)
    set(_multi_value_args DISABLE_OPTIONS)
    cmake_parse_arguments(_DB "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    # Validate required arguments
    if(NOT _DB_OUTPUT)
        message(FATAL_ERROR "generate_compilation_database: OUTPUT argument is required")
    endif()
    if(NOT _DB_TEMP_DIR)
        message(FATAL_ERROR "generate_compilation_database: TEMP_DIR argument is required")
    endif()
    if(NOT _DB_LOG_FILE)
        message(FATAL_ERROR "generate_compilation_database: LOG_FILE argument is required")
    endif()
    if(NOT _DB_COMMENT)
        set(_DB_COMMENT "Generating compilation database")
    endif()

    # Build the cmake configure command
    # NOTE: CMAKE_EXPORT_COMPILE_COMMANDS generates compile_commands.json with absolute paths,
    # but uses the build directory as the "directory" field. We need to fix this later
    # to use CMAKE_SOURCE_DIR instead, so that LibTooling-based tools run from the project root.
    set(_cmake_configure_args
        "-G" "Ninja"
        "-S" "${CMAKE_SOURCE_DIR}"
        "-B" "${_DB_TEMP_DIR}"
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_BUILD_TYPE=Debug"
        "-DUSE_MUSL=OFF"
        "-DASCIICHAT_KEEP_SYSROOT_FOR_TOOLS=ON"
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    )

    # On macOS, detect and pass SDK path for proper system header resolution
    # The defer tool (ascii-instr-defer) uses libclang which needs the SDK path
    # to find system headers like stdbool.h
    if(APPLE)
        # First, try to use the SDK path saved by LLVM.cmake (before it was cleared)
        set(_macos_sdk_for_db "")

        if(ASCIICHAT_MACOS_SDK_FOR_TOOLS)
            # This is the SDK path saved in configure_llvm_post_project() before clearing CMAKE_OSX_SYSROOT
            set(_macos_sdk_for_db "${ASCIICHAT_MACOS_SDK_FOR_TOOLS}")
        elseif(CMAKE_OSX_SYSROOT)
            # Use current CMAKE_OSX_SYSROOT if set
            set(_macos_sdk_for_db "${CMAKE_OSX_SYSROOT}")
        elseif(DEFINED ENV{HOMEBREW_SDKROOT})
            # Try Homebrew's environment variable
            set(_macos_sdk_for_db "$ENV{HOMEBREW_SDKROOT}")
        else()
            # Final fallback: detect using xcrun
            find_program(_XCRUN_EXECUTABLE xcrun)
            if(_XCRUN_EXECUTABLE)
                execute_process(
                    COMMAND ${_XCRUN_EXECUTABLE} --show-sdk-path
                    OUTPUT_VARIABLE _MACOS_SDK_PATH
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                )
                if(_MACOS_SDK_PATH AND EXISTS "${_MACOS_SDK_PATH}")
                    set(_macos_sdk_for_db "${_MACOS_SDK_PATH}")
                endif()
            endif()
        endif()

        # Pass SDK path to the temp cmake build
        # Note: LLVM.cmake will clear CMAKE_OSX_SYSROOT in main build, but temp build needs it for header detection
        if(_macos_sdk_for_db)
            list(APPEND _cmake_configure_args "-DCMAKE_OSX_SYSROOT=${_macos_sdk_for_db}")
            message(STATUS "Using macOS SDK for compilation database: ${_macos_sdk_for_db}")
        else()
            message(STATUS "Could not detect macOS SDK path, relying on CMake's default SDK detection for compilation database")
        endif()

        # Also pass CMAKE_OSX_ARCHITECTURES to prevent cmake from adding conflicting flags
        if(CMAKE_OSX_ARCHITECTURES)
            list(APPEND _cmake_configure_args "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
        endif()
    endif()

    if(WIN32)
        list(APPEND _cmake_configure_args "-DCMAKE_RC_COMPILER=CMAKE_RC_COMPILER-NOTFOUND")
    endif()

    # Add disable options
    foreach(_opt IN LISTS _DB_DISABLE_OPTIONS)
        list(APPEND _cmake_configure_args "-D${_opt}=OFF")
    endforeach()

    # Detect clang resource directory for compilation database
    # Required for LibTooling tools to find clang's builtin headers (stddef.h, stdbool.h, etc.)
    # Only pass this to the temporary cmake, not to the main build's cache
    if(CMAKE_C_COMPILER MATCHES "clang")
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
            OUTPUT_VARIABLE _detected_resource_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_detected_resource_dir AND EXISTS "${_detected_resource_dir}")
            list(APPEND _cmake_configure_args "-DCLANG_RESOURCE_DIR=${_detected_resource_dir}")
        endif()
    endif()

    # Convert to space-separated string for shell command
    list(JOIN _cmake_configure_args " " _cmake_args_str)

    # Build the cmake build command
    set(_cmake_build_cmd "${CMAKE_COMMAND} --build ${_DB_TEMP_DIR} --target generate_version")

    if(WIN32)
        # Windows: use cmd to redirect stdout and stderr to log file
        add_custom_command(
            OUTPUT "${_DB_OUTPUT}"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${_DB_TEMP_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_DB_TEMP_DIR}"
            COMMAND cmd /c "${CMAKE_COMMAND} ${_cmake_args_str} > ${_DB_LOG_FILE} 2>&1"
            COMMAND cmd /c "${_cmake_build_cmd} >> ${_DB_LOG_FILE} 2>&1"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${_DB_TEMP_DIR}/compile_commands.json"
                "${_DB_OUTPUT}"
            COMMENT "${_DB_COMMENT}"
            VERBATIM
        )
    else()
        # Unix: use shell redirection
        add_custom_command(
            OUTPUT "${_DB_OUTPUT}"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${_DB_TEMP_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_DB_TEMP_DIR}"
            COMMAND sh -c "${CMAKE_COMMAND} ${_cmake_args_str} > ${_DB_LOG_FILE} 2>&1"
            COMMAND sh -c "${_cmake_build_cmd} >> ${_DB_LOG_FILE} 2>&1"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${_DB_TEMP_DIR}/compile_commands.json"
                "${_DB_OUTPUT}"
            COMMENT "${_DB_COMMENT}"
            VERBATIM
        )
    endif()
endfunction()

# =============================================================================
# Helper function to detect Clang resource directory
# =============================================================================
# Detects the Clang resource directory for LibTooling-based tools.
# Required for finding builtin headers like stdatomic.h.
#
# Usage:
#   detect_clang_resource_dir(OUTPUT_VAR)
#
# Returns:
#   Sets OUTPUT_VAR to the resource directory path, or empty if not found
#
function(detect_clang_resource_dir _output_var)
    set(_clang_for_db "${CMAKE_C_COMPILER}")

    execute_process(
        COMMAND ${_clang_for_db} -print-resource-dir
        OUTPUT_VARIABLE _resource_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(_resource_dir)
        set(${_output_var} "${_resource_dir}" PARENT_SCOPE)
    else()
        set(${_output_var} "" PARENT_SCOPE)
    endif()
endfunction()

