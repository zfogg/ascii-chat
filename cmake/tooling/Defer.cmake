include_guard(GLOBAL)

option(ASCII_BUILD_WITH_DEFER "Enable defer() transformation during build" ON)
set(ASCII_DEFER_TOOL "" CACHE FILEPATH "Path to pre-built ascii-instr-defer tool (optional)")

include(${CMAKE_SOURCE_DIR}/cmake/tooling/Targets.cmake)

function(ascii_defer_prepare)
    if(NOT ASCII_BUILD_WITH_DEFER)
        set(ASCII_DEFER_ENABLED FALSE PARENT_SCOPE)
        return()
    endif()

    # Determine which defer tool to use
    if(ASCII_DEFER_TOOL AND EXISTS "${ASCII_DEFER_TOOL}")
        set(_defer_tool_exe "${ASCII_DEFER_TOOL}")
        set(_defer_tool_depends "")
        message(STATUS "Using external defer tool: ${_defer_tool_exe}")
    else()
        ascii_add_tooling_targets()
        set(_defer_tool_exe $<TARGET_FILE:ascii-instr-defer>)
        set(_defer_tool_depends ascii-instr-defer)
        message(STATUS "Building defer tool from source")
    endif()


    set(USE_PRECOMPILED_HEADERS OFF CACHE BOOL "Disable PCH when defer transformation is enabled" FORCE)

    set(defer_transformed_dir "${CMAKE_BINARY_DIR}/defer_transformed")

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
                file(STRINGS "${abs_path}" _defer_usage REGEX "defer\\(")
                if(_defer_usage)
                    set(generated_path "${defer_transformed_dir}/${rel_path}")
                    list(APPEND defer_abs_paths "${abs_path}")
                    list(APPEND defer_rel_paths "${rel_path}")
                    list(APPEND defer_generated_paths "${generated_path}")
                    message(STATUS "Found defer() usage in: ${rel_path}")
                endif()
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES defer_abs_paths)
    list(REMOVE_DUPLICATES defer_rel_paths)
    list(REMOVE_DUPLICATES defer_generated_paths)

    # Check if any files actually use defer()
    list(LENGTH defer_rel_paths _defer_file_count)
    if(_defer_file_count EQUAL 0)
        # No files use defer(), so defer transformation is not needed
        message(STATUS "No source files use defer() - defer runtime will not be included")
        set(ASCII_DEFER_ENABLED FALSE PARENT_SCOPE)
        return()
    endif()

    # Files use defer(), so we need the runtime
    # Add defer.c to CORE_SRCS if not already present
    # Explicitly read CORE_SRCS from parent scope to ensure correct behavior
    set(_core_srcs "${CORE_SRCS}")
    if(NOT "lib/tooling/defer/defer.c" IN_LIST _core_srcs)
        list(APPEND _core_srcs "lib/tooling/defer/defer.c")
        set(CORE_SRCS "${_core_srcs}" PARENT_SCOPE)
        message(STATUS "Added defer runtime (lib/tooling/defer/defer.c) to CORE_SRCS")
    endif()

    # Directories that must be copied to transformed tree (for includes)
    set(_ascii_defer_copy_dirs
        "lib/tooling"
    )

    # Generate compilation database for the defer tool
    set(_ascii_temp_build_dir "${CMAKE_BINARY_DIR}/compile_db_temp_defer")
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/compile_commands_defer.json"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_ascii_temp_build_dir}"
        COMMAND ${CMAKE_COMMAND} -G Ninja
            -S "${CMAKE_SOURCE_DIR}"
            -B "${_ascii_temp_build_dir}"
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_RC_COMPILER=CMAKE_RC_COMPILER-NOTFOUND
            -DASCII_BUILD_WITH_DEFER=OFF
            -DUSE_PRECOMPILED_HEADERS=OFF
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        COMMAND ${CMAKE_COMMAND} --build "${_ascii_temp_build_dir}" --target generate_version
        COMMAND ${CMAKE_COMMAND} -E copy
            "${_ascii_temp_build_dir}/compile_commands.json"
            "${CMAKE_BINARY_DIR}/compile_commands_defer.json"
        COMMENT "Generating compilation database for defer transformation tool"
        VERBATIM
    )

    # Build list of excluded files to copy
    if(NOT TARGET ascii-defer-transform-timer-start)
        add_custom_target(ascii-defer-transform-timer-start
            COMMAND ${CMAKE_COMMAND}
                -DACTION=start
                -DTARGET_NAME=defer-all
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                -DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
                -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
            COMMENT "Starting defer() transformation timing block"
            VERBATIM
        )
    endif()

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
            COMMAND ${_defer_tool_exe} "${_rel_path}" --output-dir=${defer_transformed_dir} -p ${CMAKE_BINARY_DIR}/compile_db_temp_defer
            DEPENDS ascii-defer-transform-timer-start ${_defer_tool_depends} "${_abs_path}" "${CMAKE_BINARY_DIR}/compile_commands_defer.json"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Transforming defer() in ${_rel_path}"
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

    add_custom_target(ascii-defer-transform-timer-end
        COMMAND ${CMAKE_COMMAND}
            -DACTION=end
            -DTARGET_NAME=defer-all
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
        COMMENT "Finishing defer() transformation timing block"
        VERBATIM
        DEPENDS ${_all_generated_outputs}
    )
    add_dependencies(ascii-defer-transform-timer-end ascii-defer-transform-timer-start)

    add_custom_target(ascii-generate-defer-transformed-sources
        DEPENDS ascii-defer-transform-timer-end
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

    set(ASCII_DEFER_ENABLED TRUE PARENT_SCOPE)
    set(ASCII_DEFER_SOURCE_DIR "${defer_transformed_dir}" PARENT_SCOPE)
endfunction()

function(ascii_defer_finalize)
    if(NOT ASCII_BUILD_WITH_DEFER)
        return()
    endif()
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

    # Link all targets with the defer runtime library
    # Note: defer.c is already compiled into ascii-chat-core (via CORE_SRCS),
    # so we don't need to link ascii-chat-defer to any targets that depend on ascii-chat-core
    # The defer runtime functions are available through ascii-chat-core
    foreach(lib_target IN LISTS ASCII_DEFER_TRANSFORM_LIBRARY_TARGETS)
        if(TARGET ${lib_target})
            add_dependencies(${lib_target} ascii-generate-defer-transformed-sources)
            target_compile_definitions(${lib_target} PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
            # defer.c is already in ascii-chat-core, so no need to link ascii-chat-defer
        endif()
    endforeach()

    foreach(exe_target IN LISTS ASCII_DEFER_TRANSFORM_EXECUTABLE_TARGETS)
        if(TARGET ${exe_target})
            add_dependencies(${exe_target} ascii-generate-defer-transformed-sources)
            target_compile_definitions(${exe_target} PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
            if(TARGET ascii-chat-defer)
                target_link_libraries(${exe_target} ascii-chat-defer)
            endif()
        endif()
    endforeach()

    if(TARGET ascii-chat-shared)
        add_dependencies(ascii-chat-shared ascii-generate-defer-transformed-sources)
        target_compile_definitions(ascii-chat-shared PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
        # Link ascii-chat-defer to provide defer runtime functions
        # Even though defer.c is in CORE_SRCS, we link ascii-chat-defer to ensure
        # the runtime functions are available (defer.c may not be compiled into core properly)
        if(TARGET ascii-chat-defer)
            target_link_libraries(ascii-chat-shared PRIVATE ascii-chat-defer)
        endif()
    endif()

endfunction()
