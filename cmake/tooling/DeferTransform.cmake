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

    # Exclude files that shouldn't be transformed
    set(_ascii_defer_excluded_sources
        "lib/platform/system.c"
        "lib/platform/posix/system.c"
        "lib/platform/posix/mutex.c"
        "lib/platform/posix/thread.c"
        "lib/platform/windows/system.c"
        "lib/platform/windows/mutex.c"
        "lib/platform/windows/thread.c"
        "lib/debug/lock.c"
        "lib/image2ascii/simd/ascii_simd.c"
        "lib/image2ascii/simd/ascii_simd_color.c"
        "lib/image2ascii/simd/common.c"
        "lib/image2ascii/simd/avx2.c"
        "lib/image2ascii/simd/sse2.c"
        "lib/image2ascii/simd/ssse3.c"
        "lib/image2ascii/simd/neon.c"
        "lib/image2ascii/simd/sve.c"
        "lib/tooling/defer/defer.c"
        "lib/tooling/defer/defer.h"
    )

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
            if(rel_path IN_LIST _ascii_defer_excluded_sources)
                continue()
            endif()

            set(generated_path "${defer_transformed_dir}/${rel_path}")
            list(APPEND defer_abs_paths "${abs_path}")
            list(APPEND defer_rel_paths "${rel_path}")
            list(APPEND defer_generated_paths "${generated_path}")
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES defer_abs_paths)
    list(REMOVE_DUPLICATES defer_rel_paths)
    list(REMOVE_DUPLICATES defer_generated_paths)

    # Add cross-platform .c files that are included by platform-specific stubs
    set(cross_platform_sources
        "lib/platform/abstraction.c"
        "lib/platform/symbols.c"
    )
    foreach(cross_plat_file IN LISTS cross_platform_sources)
        # Skip if this file is in the excluded list (will be copied instead)
        if(cross_plat_file IN_LIST _ascii_defer_excluded_sources)
            continue()
        endif()
        set(abs_path "${CMAKE_SOURCE_DIR}/${cross_plat_file}")
        if(EXISTS "${abs_path}")
            set(generated_path "${defer_transformed_dir}/${cross_plat_file}")
            list(APPEND defer_abs_paths "${abs_path}")
            list(APPEND defer_rel_paths "${cross_plat_file}")
            list(APPEND defer_generated_paths "${generated_path}")
        endif()
    endforeach()

    # Also collect all header files for transformation
    file(GLOB_RECURSE all_headers
        "${CMAKE_SOURCE_DIR}/lib/*.h"
        "${CMAKE_SOURCE_DIR}/src/*.h"
    )

    foreach(header_path IN LISTS all_headers)
        file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}" "${header_path}")
        string(REPLACE "\\" "/" rel_path "${rel_path}")

        # Skip excluded paths
        if(rel_path MATCHES "^deps/")
            continue()
        endif()
        if(rel_path MATCHES "^lib/tooling/")
            continue()
        endif()
        if(rel_path MATCHES "^lib/tests/")
            continue()
        endif()
        if(rel_path MATCHES "^src/tooling/")
            continue()
        endif()
        if(rel_path MATCHES "^build")
            continue()
        endif()

        set(generated_path "${defer_transformed_dir}/${rel_path}")
        list(APPEND defer_abs_paths "${header_path}")
        list(APPEND defer_rel_paths "${rel_path}")
        list(APPEND defer_generated_paths "${generated_path}")
    endforeach()

    list(REMOVE_DUPLICATES defer_abs_paths)
    list(REMOVE_DUPLICATES defer_rel_paths)
    list(REMOVE_DUPLICATES defer_generated_paths)

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
    set(excluded_abs_paths "")
    set(excluded_generated_paths "")
    foreach(excluded_file IN LISTS _ascii_defer_excluded_sources)
        set(src_file "${CMAKE_SOURCE_DIR}/${excluded_file}")
        if(EXISTS "${src_file}")
            list(APPEND excluded_abs_paths "${src_file}")
            set(dest_file "${defer_transformed_dir}/${excluded_file}")
            list(APPEND excluded_generated_paths "${dest_file}")
        endif()
    endforeach()

    set(_ascii_defer_excluded_file "${CMAKE_BINARY_DIR}/defer_excluded_files.txt")
    file(WRITE "${_ascii_defer_excluded_file}" "")
    foreach(excluded_file IN LISTS _ascii_defer_excluded_sources)
        file(APPEND "${_ascii_defer_excluded_file}" "${excluded_file}\n")
    endforeach()

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

    # Copy excluded files individually (these don't need transformation)
    foreach(excluded_file IN LISTS _ascii_defer_excluded_sources)
        set(src_file "${CMAKE_SOURCE_DIR}/${excluded_file}")
        if(EXISTS "${src_file}")
            set(dest_file "${defer_transformed_dir}/${excluded_file}")
            get_filename_component(dest_dir "${dest_file}" DIRECTORY)

            add_custom_command(
                OUTPUT "${dest_file}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${dest_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy "${src_file}" "${dest_file}"
                DEPENDS "${src_file}"
                COMMENT "Copying excluded file ${excluded_file}"
                VERBATIM
            )
            list(APPEND _all_generated_outputs "${dest_file}")
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
            elseif(rel_path IN_LIST _ascii_defer_excluded_sources)
                # Excluded source file - use copied version in transformed dir
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

function(_ascii_defer_strip_source_include_dirs target_name)
    if(NOT TARGET ${target_name})
        return()
    endif()
    get_target_property(_ascii_existing_includes ${target_name} INCLUDE_DIRECTORIES)
    if(NOT _ascii_existing_includes OR _ascii_existing_includes STREQUAL "NOTFOUND")
        return()
    endif()
    set(_ascii_filtered_includes "")
    foreach(_ascii_dir IN LISTS _ascii_existing_includes)
        if(NOT _ascii_dir)
            continue()
        endif()
        string(REPLACE "\\" "/" _ascii_normalized "${_ascii_dir}")
        if(_ascii_normalized STREQUAL "${CMAKE_SOURCE_DIR}/lib")
            continue()
        endif()
        if(_ascii_normalized STREQUAL "${CMAKE_SOURCE_DIR}/src")
            continue()
        endif()
        list(APPEND _ascii_filtered_includes "${_ascii_dir}")
    endforeach()
    set_target_properties(${target_name} PROPERTIES INCLUDE_DIRECTORIES "${_ascii_filtered_includes}")
endfunction()

function(ascii_defer_finalize)
    if(NOT ASCII_BUILD_WITH_DEFER)
        return()
    endif()
    if(NOT TARGET ascii-generate-defer-transformed-sources)
        return()
    endif()

    set(defer_transformed_dir "${CMAKE_BINARY_DIR}/defer_transformed")

    set(defer_include_dirs
        "${defer_transformed_dir}/lib"
        "${defer_transformed_dir}/src"
    )

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

    # Tooling runtime targets need original headers
    if(TARGET ascii-tooling-defer-runtime)
        target_include_directories(ascii-tooling-defer-runtime PRIVATE
            ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_SOURCE_DIR}/src
        )
    endif()

    foreach(lib_target IN LISTS ASCII_DEFER_TRANSFORM_LIBRARY_TARGETS)
        if(TARGET ${lib_target})
            add_dependencies(${lib_target} ascii-generate-defer-transformed-sources)
            _ascii_defer_strip_source_include_dirs(${lib_target})
            # Prepend transformed include paths so transformed headers are found first
            target_include_directories(${lib_target} BEFORE PRIVATE ${defer_include_dirs})
            target_compile_definitions(${lib_target} PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
            if(TARGET ascii-tooling-defer-runtime)
                target_link_libraries(${lib_target} PUBLIC ascii-tooling-defer-runtime)
            endif()
        endif()
    endforeach()

    foreach(exe_target IN LISTS ASCII_DEFER_TRANSFORM_EXECUTABLE_TARGETS)
        if(TARGET ${exe_target})
            add_dependencies(${exe_target} ascii-generate-defer-transformed-sources)
            _ascii_defer_strip_source_include_dirs(${exe_target})
            # Prepend transformed include paths for executables too
            target_include_directories(${exe_target} BEFORE PRIVATE ${defer_include_dirs})
            target_compile_definitions(${exe_target} PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
            if(TARGET ascii-tooling-defer-runtime)
                target_link_libraries(${exe_target} ascii-tooling-defer-runtime)
            endif()
        endif()
    endforeach()

    if(TARGET ascii-chat-shared)
        add_dependencies(ascii-chat-shared ascii-generate-defer-transformed-sources)
        _ascii_defer_strip_source_include_dirs(ascii-chat-shared)
        target_include_directories(ascii-chat-shared BEFORE PRIVATE ${defer_include_dirs})
        target_compile_definitions(ascii-chat-shared PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
        if(TARGET ascii-tooling-defer-runtime)
            target_link_libraries(ascii-chat-shared PRIVATE ascii-tooling-defer-runtime)
        endif()
    endif()

endfunction()
