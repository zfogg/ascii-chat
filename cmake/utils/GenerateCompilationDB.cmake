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
#       [DISABLE_OPTIONS <opt1> <opt2>...]  # Options to disable (e.g., "ASCIICHAT_USE_PCH")
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
#       DISABLE_OPTIONS ASCIICHAT_USE_PCH ASCIICHAT_ENABLE_ANALYZERS
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
    #
    # IMPORTANT: Use the same CMAKE_BUILD_TYPE as the parent build so that vcpkg finds
    # the correct libraries (static for Release, dynamic for Debug/Dev).
    set(_temp_build_type "${CMAKE_BUILD_TYPE}")
    if(NOT _temp_build_type)
        set(_temp_build_type "Debug")
    endif()
    set(_cmake_configure_args
        "-G" "Ninja"
        "-S" "${CMAKE_SOURCE_DIR}"
        "-B" "${_DB_TEMP_DIR}"
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_BUILD_TYPE=${_temp_build_type}"
        "-DUSE_MUSL=OFF"
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    )

    # Pass dependency cache directory so temp build finds cached dependencies (mimalloc, etc.)
    if(ASCIICHAT_DEPS_CACHE_DIR)
        list(APPEND _cmake_configure_args "-DASCIICHAT_DEPS_CACHE_DIR=${ASCIICHAT_DEPS_CACHE_DIR}")
    endif()
    if(ASCIICHAT_DEPS_CACHE_ROOT)
        list(APPEND _cmake_configure_args "-DASCIICHAT_DEPS_CACHE_ROOT=${ASCIICHAT_DEPS_CACHE_ROOT}")
    endif()

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
            # Also pass it as ASCIICHAT_MACOS_SDK_FOR_TOOLS so LLVM.cmake can find it
            list(APPEND _cmake_configure_args "-DASCIICHAT_MACOS_SDK_FOR_TOOLS=${_macos_sdk_for_db}")
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
        # Pass vcpkg configuration to temp build so dependencies are found
        if(DEFINED VCPKG_TARGET_TRIPLET)
            list(APPEND _cmake_configure_args "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
        endif()
        if(DEFINED VCPKG_INSTALLED_DIR)
            list(APPEND _cmake_configure_args "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}")
        endif()
        if(DEFINED ENV{VCPKG_ROOT})
            list(APPEND _cmake_configure_args "-DCMAKE_TOOLCHAIN_FILE=$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        endif()
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
        # Always pass the resource dir to the temp build, even if it doesn't exist on this machine
        # The temp build's CMake will re-detect it using its own compiler
        if(_detected_resource_dir)
            list(APPEND _cmake_configure_args "-DCLANG_RESOURCE_DIR=${_detected_resource_dir}")
        endif()
    endif()

    # Convert to space-separated string for shell command
    list(JOIN _cmake_configure_args " " _cmake_args_str)

    # Build the cmake build command
    set(_cmake_build_cmd "${CMAKE_COMMAND} --build ${_DB_TEMP_DIR} --target generate_version")

    # Write cmake arguments to a response file for the helper script
    # This avoids issues with complex quoting and special characters
    set(_args_file "${CMAKE_BINARY_DIR}/compile_db_args.txt")
    list(JOIN _cmake_configure_args "\n" _args_content)
    file(WRITE "${_args_file}" "${_args_content}")

    # Use CMake script helper for consistent error handling on all platforms
    # This properly captures output, shows log on failure, and propagates exit codes
    add_custom_command(
        OUTPUT "${_DB_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_DB_TEMP_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_DB_TEMP_DIR}"
        COMMAND ${CMAKE_COMMAND}
            -DARGS_FILE=${_args_file}
            -DLOG_FILE=${_DB_LOG_FILE}
            -DAPPEND_TO_LOG=FALSE
            -DOPERATION_NAME=CMake_configure_for_compilation_database
            -P ${CMAKE_SOURCE_DIR}/cmake/utils/RunCMakeWithLog.cmake
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${_DB_TEMP_DIR}
            -DBUILD_TARGET=generate_version
            -DLOG_FILE=${_DB_LOG_FILE}
            -DAPPEND_TO_LOG=TRUE
            -DOPERATION_NAME=CMake_build_generate_version
            -P ${CMAKE_SOURCE_DIR}/cmake/utils/RunCMakeBuildWithLog.cmake
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${_DB_TEMP_DIR}/compile_commands.json
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/utils/FixCompilationDBDirectory.cmake
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${_DB_TEMP_DIR}/compile_commands.json
            -P ${CMAKE_SOURCE_DIR}/cmake/utils/ConvertCompilationDBFormat.cmake
        COMMAND ${CMAKE_COMMAND} -E copy
            "${_DB_TEMP_DIR}/compile_commands.json"
            "${_DB_OUTPUT}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${_DB_OUTPUT}
            -P ${CMAKE_SOURCE_DIR}/cmake/utils/ConvertCompilationDBFormat.cmake
        COMMAND ${CMAKE_COMMAND} -E copy
            "${_DB_OUTPUT}"
            "${_DB_TEMP_DIR}/compile_commands.json"
        COMMENT "${_DB_COMMENT}"
        VERBATIM
    )
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

