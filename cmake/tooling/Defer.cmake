include_guard(GLOBAL)

# defer() is MANDATORY - the code requires it to function correctly
# Build the defer tool as an external project to avoid inheriting musl/static flags
# Defer transformation is ALWAYS enabled - no option to disable it

# Option to prefer static Clang libraries over shared libclang-cpp.so
# When ON, produces a larger (~44MB) but self-contained binary
# When OFF (default), prefers smaller binary (~300K) using shared libs
option(ASCIICHAT_DEFER_PREFER_STATIC "Prefer static Clang libraries for defer tool (larger binary)" OFF)

set(ASCIICHAT_DEFER_TOOL "" CACHE FILEPATH "Path to pre-built ascii-instr-defer tool (optional)")

include(${CMAKE_SOURCE_DIR}/cmake/utils/BuildLLVMTool.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/utils/TimerTargets.cmake)

function(ascii_defer_prepare)

    # Build extra cmake args for the defer tool
    set(_defer_extra_args "")
    if(ASCIICHAT_DEFER_PREFER_STATIC)
        list(APPEND _defer_extra_args -DDEFER_TOOL_PREFER_STATIC=ON)
    endif()

    # Build/find the defer tool using the common utility
    build_llvm_tool(
        NAME defer
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/tooling/defer"
        CACHE_DIR_NAME "defer-tool"
        OUTPUT_EXECUTABLE "ascii-instr-defer"
        PREBUILT_VAR ASCIICHAT_DEFER_TOOL
        PASS_LLVM_CONFIG
        ENABLE_LOG_OUTPUT
        ISOLATE_FROM_ENV
        CLEAN_INCOMPLETE_CACHE
        EXTRA_CMAKE_ARGS ${_defer_extra_args}
    )

    set(_defer_tool_exe "${DEFER_TOOL_EXECUTABLE}")
    set(_defer_tool_depends "${DEFER_TOOL_DEPENDS}")


    set(defer_transformed_dir "${CMAKE_BINARY_DIR}/defer_transformed")

    # Defer always processes original sources from CMAKE_SOURCE_DIR
    # When panic is also enabled, panic will process defer's output
    set(_defer_input_root "${CMAKE_SOURCE_DIR}")

    # Scan source files to find which ones actually use defer()
    # Only transform files that contain "defer(" to minimize build time
    set(candidate_vars
        UTIL_SRCS
        CRYPTO_SRCS
        PLATFORM_SRCS
        SIMD_SRCS
        VIDEO_SRCS
        AUDIO_SRCS
        NETWORK_SRCS
        CORE_SRCS
        DATA_STRUCTURES_SRCS
        APP_SRCS
    )

    set(defer_abs_paths "")
    set(defer_rel_paths "")
    set(defer_generated_paths "")

    foreach(var IN LISTS candidate_vars)
        if(NOT DEFINED ${var})
            continue()
        endif()
        foreach(item IN LISTS ${var})
            if(item STREQUAL "")
                continue()
            endif()
            if(IS_ABSOLUTE "${item}")
                set(abs_path "${item}")
            else()
                set(abs_path "${CMAKE_SOURCE_DIR}/${item}")
            endif()
            file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}" "${abs_path}")
            string(REPLACE "\\" "/" rel_path "${rel_path}")

            if(rel_path MATCHES "^deps/")
                continue()
            endif()
            if(rel_path MATCHES "^lib/tooling/")
                continue()
            endif()

            # Only transform files that actually use defer()
            if(EXISTS "${abs_path}")
                file(STRINGS "${abs_path}" _defer_usage REGEX "(^|[^a-zA-Z_])defer\\(")
                if(_defer_usage)
                    set(generated_path "${defer_transformed_dir}/${rel_path}")
                    list(APPEND defer_abs_paths "${abs_path}")
                    list(APPEND defer_rel_paths "${rel_path}")
                    list(APPEND defer_generated_paths "${generated_path}")
                    # Found defer() usage - processing silently
                endif()
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES defer_abs_paths)
    list(REMOVE_DUPLICATES defer_rel_paths)
    list(REMOVE_DUPLICATES defer_generated_paths)

    # Note: With direct code insertion, no runtime library is needed.
    # The defer transformer inserts cleanup code directly at each exit point.

    # Use CMake's standard compilation database
    # CMake generates compile_commands.json in the build directory
    set(_ASCII_COMPILE_DB "${CMAKE_BINARY_DIR}/compile_commands.json")

    # Create timer targets for defer transformation (comments disabled for quiet builds)
    add_timer_targets(
        NAME defer-all
        COMMENT_START ""
        COMMENT_END ""
    )

    # Create per-file transformation commands for incremental builds
    set(_all_generated_outputs "")

    # Transform each source file individually
    list(LENGTH defer_rel_paths _num_files)
    math(EXPR _last_idx "${_num_files} - 1")
    foreach(_idx RANGE 0 ${_last_idx})
        list(GET defer_abs_paths ${_idx} _abs_path)
        list(GET defer_rel_paths ${_idx} _rel_path)
        list(GET defer_generated_paths ${_idx} _gen_path)

        # Create output directory path
        get_filename_component(_gen_dir "${_gen_path}" DIRECTORY)

        add_custom_command(
            OUTPUT "${_gen_path}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen_dir}"
            COMMAND echo "DEBUG: Transforming ${_rel_path} with compilation db at ${CMAKE_BINARY_DIR}"
            COMMAND python3 "${CMAKE_SOURCE_DIR}/cmake/utils/run_defer_with_includes.py" "${CMAKE_BINARY_DIR}" "${_defer_tool_exe}" "${_rel_path}" --output-dir=${defer_transformed_dir}
            DEPENDS defer-all-timer-start ${_defer_tool_depends} "${_abs_path}" "${_ASCII_COMPILE_DB}"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Defer: ${_rel_path}"
            VERBATIM
        )
        list(APPEND _all_generated_outputs "${_gen_path}")
    endforeach()

    # Copy entire directories to transformed tree (tooling runtime headers)
    foreach(copy_dir IN LISTS _ascii_defer_copy_dirs)
        set(src_dir "${CMAKE_SOURCE_DIR}/${copy_dir}")
        if(IS_DIRECTORY "${src_dir}")
            set(dest_dir "${defer_transformed_dir}/${copy_dir}")
            # Use a stamp file to track directory copy
            set(stamp_file "${CMAKE_BINARY_DIR}/${copy_dir}_copy.stamp")
            file(GLOB_RECURSE _dir_files "${src_dir}/*")

            add_custom_command(
                OUTPUT "${stamp_file}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${src_dir}" "${dest_dir}"
                COMMAND ${CMAKE_COMMAND} -E touch "${stamp_file}"
                DEPENDS ${_dir_files}
                COMMENT "Copying directory ${copy_dir} to transformed tree"
                VERBATIM
            )
            list(APPEND _all_generated_outputs "${stamp_file}")
        endif()
    endforeach()

    # Create a target that depends on all generated outputs
    # Note: add_custom_target DEPENDS accepts file paths, add_dependencies does not
    add_custom_target(defer-all-generated-outputs
        DEPENDS ${_all_generated_outputs}
    )
    add_dependencies(defer-all-timer-end defer-all-generated-outputs)

    add_custom_target(ascii-generate-defer-transformed-sources
        DEPENDS defer-all-timer-end
    )

    # Replace source file paths with transformed versions
    foreach(var IN LISTS candidate_vars)
        if(NOT DEFINED ${var})
            continue()
        endif()
        set(updated_list "")
        foreach(item IN LISTS ${var})
            if(item STREQUAL "")
                continue()
            endif()
            if(IS_ABSOLUTE "${item}")
                file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}" "${item}")
            else()
                set(rel_path "${item}")
            endif()
            string(REPLACE "\\" "/" rel_path "${rel_path}")
            if(rel_path IN_LIST defer_rel_paths)
                # Transformed source file
                set(generated_path "${defer_transformed_dir}/${rel_path}")
                list(APPEND updated_list "${generated_path}")
                set_source_files_properties("${generated_path}" PROPERTIES GENERATED TRUE)
            else()
                # Other files (deps, etc.) - use original
                list(APPEND updated_list "${item}")
            endif()
        endforeach()
        set(${var} "${updated_list}" PARENT_SCOPE)
    endforeach()

    # Collect unique source directories that contain defer-transformed files
    # These need to be added as include paths so relative includes work
    # Use hardcoded paths to support flexible header directory structure
    set(defer_source_dirs "")

    # Add standard include directories (mirrors Include.cmake exactly)
    list(APPEND defer_source_dirs
        "${CMAKE_SOURCE_DIR}/include"  # Public API headers
        "${CMAKE_SOURCE_DIR}/lib"      # Private implementation headers
        "${CMAKE_SOURCE_DIR}/src"      # Application headers
    )

    # Also add directories where defer-transformed sources live
    # (for local headers in the same directory as .c files)
    foreach(rel_path IN LISTS defer_rel_paths)
        get_filename_component(dir_path "${rel_path}" DIRECTORY)
        if(dir_path AND NOT "${CMAKE_SOURCE_DIR}/${dir_path}" IN_LIST defer_source_dirs)
            list(APPEND defer_source_dirs "${CMAKE_SOURCE_DIR}/${dir_path}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES defer_source_dirs)

    set(ASCII_DEFER_SOURCE_DIR "${defer_transformed_dir}" PARENT_SCOPE)
    set(ASCII_DEFER_INCLUDE_DIRS "${defer_source_dirs}" PARENT_SCOPE)
endfunction()

function(ascii_defer_finalize)
    if(NOT TARGET ascii-generate-defer-transformed-sources)
        return()
    endif()

    set(ASCII_DEFER_TRANSFORM_LIBRARY_TARGETS
        ascii-chat-util
        ascii-chat-data-structures
        ascii-chat-platform
        ascii-chat-crypto
        ascii-chat-simd
        ascii-chat-video
        ascii-chat-audio
        ascii-chat-core
        ascii-chat-network
    )

    set(ASCII_DEFER_TRANSFORM_EXECUTABLE_TARGETS
        ascii-chat
    )

    # Add compile definitions, dependencies, and include directories for defer-transformed targets
    # Note: No runtime library is needed - defer cleanup is inlined directly into the code
    # Include directories are needed so relative includes like #include "foo.h" work in transformed files
    foreach(lib_target IN LISTS ASCII_DEFER_TRANSFORM_LIBRARY_TARGETS)
        if(TARGET ${lib_target})
            add_dependencies(${lib_target} ascii-generate-defer-transformed-sources)
            target_compile_definitions(${lib_target} PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
            if(ASCII_DEFER_INCLUDE_DIRS)
                target_include_directories(${lib_target} PRIVATE ${ASCII_DEFER_INCLUDE_DIRS})
            endif()
        endif()
    endforeach()

    foreach(exe_target IN LISTS ASCII_DEFER_TRANSFORM_EXECUTABLE_TARGETS)
        if(TARGET ${exe_target})
            add_dependencies(${exe_target} ascii-generate-defer-transformed-sources)
            target_compile_definitions(${exe_target} PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
            if(ASCII_DEFER_INCLUDE_DIRS)
                target_include_directories(${exe_target} PRIVATE ${ASCII_DEFER_INCLUDE_DIRS})
            endif()
        endif()
    endforeach()

    if(TARGET ascii-chat-shared)
        add_dependencies(ascii-chat-shared ascii-generate-defer-transformed-sources)
        target_compile_definitions(ascii-chat-shared PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
        if(ASCII_DEFER_INCLUDE_DIRS)
            target_include_directories(ascii-chat-shared PRIVATE ${ASCII_DEFER_INCLUDE_DIRS})
        endif()
    endif()

endfunction()
