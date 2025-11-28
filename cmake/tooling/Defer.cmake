include_guard(GLOBAL)

# defer() is MANDATORY - the code requires it to function correctly
# Build the defer tool as an external project to avoid inheriting musl/static flags

set(ASCIICHAT_DEFER_TOOL "" CACHE FILEPATH "Path to pre-built ascii-instr-defer tool (optional)")

include(ExternalProject)

function(ascii_defer_prepare)
    # Determine which defer tool to use
    if(ASCIICHAT_DEFER_TOOL AND EXISTS "${ASCIICHAT_DEFER_TOOL}")
        set(_defer_tool_exe "${ASCIICHAT_DEFER_TOOL}")
        set(_defer_tool_depends "")
        message(STATUS "Using external defer tool: ${_defer_tool_exe}")
    else()
        # Build defer tool as external project with clean CMake environment
        set(_defer_build_dir "${CMAKE_BINARY_DIR}/defer_tool_build")
        set(_defer_tool_exe "${_defer_build_dir}/ascii-instr-defer${CMAKE_EXECUTABLE_SUFFIX}")

        # Detect the C++ compiler (use same compiler family as main build)
        if(CMAKE_CXX_COMPILER)
            set(_defer_cxx_compiler "${CMAKE_CXX_COMPILER}")
        elseif(CMAKE_C_COMPILER MATCHES "clang")
            get_filename_component(_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
            find_program(_defer_cxx_compiler NAMES clang++ PATHS "${_compiler_dir}" NO_DEFAULT_PATH)
            if(NOT _defer_cxx_compiler)
                find_program(_defer_cxx_compiler NAMES clang++)
            endif()
        else()
            find_program(_defer_cxx_compiler NAMES c++ g++)
        endif()

        # Detect vcpkg if on Windows - pass vcpkg paths but not the toolchain file
        # The vcpkg toolchain file adds -nostartfiles -nostdlib which breaks the defer tool build
        set(_defer_cmake_args
            -DCMAKE_CXX_COMPILER=${_defer_cxx_compiler}
            -DCMAKE_BUILD_TYPE=Release
            -DOUTPUT_DIR=${_defer_build_dir}
        )

        # On Windows with vcpkg, pass the vcpkg root so the defer tool can find dependencies
        # but don't use the toolchain file itself (to avoid inherited build flags)
        if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
            list(APPEND _defer_cmake_args
                -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
                -DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}
            )
            # Force empty linker flags BEFORE project() via cache to prevent -nostartfiles -nostdlib
            # These are set in the external project's initial cache file
            set(_defer_cache_args
                -DCMAKE_CXX_COMPILER_WORKS:BOOL=TRUE
                -DCMAKE_C_COMPILER_WORKS:BOOL=TRUE
                -DCMAKE_EXE_LINKER_FLAGS:STRING=
                -DCMAKE_EXE_LINKER_FLAGS_DEBUG:STRING=
                -DCMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=
                -DCMAKE_SHARED_LINKER_FLAGS:STRING=
                -DCMAKE_MODULE_LINKER_FLAGS:STRING=
                # Prevent CMake from adding platform-default linker flags
                -DCMAKE_PLATFORM_REQUIRED_RUNTIME_PATH:STRING=
            )
        else()
            set(_defer_cache_args "")
        endif()

        ExternalProject_Add(ascii-instr-defer-external
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/tooling/defer"
            BINARY_DIR "${_defer_build_dir}"
            CMAKE_ARGS ${_defer_cmake_args}
            CMAKE_CACHE_ARGS ${_defer_cache_args}
            BUILD_ALWAYS FALSE
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${_defer_tool_exe}"
        )

        set(_defer_tool_depends ascii-instr-defer-external)
        message(STATUS "Building defer tool as external project")
    endif()


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

    # Note: With direct code insertion, no runtime library is needed.
    # The defer transformer inserts cleanup code directly at each exit point.

    # Detect Clang resource directory early for compilation database generation
    if(WIN32)
        set(_clang_for_db "${CMAKE_C_COMPILER}")
    else()
        set(_clang_for_db "/usr/bin/clang")
    endif()

    execute_process(
        COMMAND ${_clang_for_db} -print-resource-dir
        OUTPUT_VARIABLE _clang_resource_dir_db
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(_clang_resource_dir_db)
        set(_defer_cflags "-resource-dir=${_clang_resource_dir_db}")
        message(STATUS "Will use Clang resource directory in compilation database: ${_clang_resource_dir_db}")
    else()
        set(_defer_cflags "")
    endif()

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
            -DCMAKE_BUILD_TYPE=Debug
            -DCMAKE_RC_COMPILER=CMAKE_RC_COMPILER-NOTFOUND
            -DUSE_MUSL=OFF
            -DASCII_BUILD_WITH_DEFER=OFF
            -DASCIICHAT_USE_PCH=OFF
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
            "-DCMAKE_C_FLAGS=${_defer_cflags}"
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

    # Add compile definition so defer() macro knows transformation is enabled
    add_compile_definitions(ASCII_BUILD_WITH_DEFER)

    set(ASCII_DEFER_ENABLED TRUE PARENT_SCOPE)
    set(ASCII_DEFER_SOURCE_DIR "${defer_transformed_dir}" PARENT_SCOPE)
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

    # Add compile definitions and dependencies for defer-transformed targets
    # Note: No runtime library is needed - defer cleanup is inlined directly into the code
    foreach(lib_target IN LISTS ASCII_DEFER_TRANSFORM_LIBRARY_TARGETS)
        if(TARGET ${lib_target})
            add_dependencies(${lib_target} ascii-generate-defer-transformed-sources)
            target_compile_definitions(${lib_target} PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
        endif()
    endforeach()

    foreach(exe_target IN LISTS ASCII_DEFER_TRANSFORM_EXECUTABLE_TARGETS)
        if(TARGET ${exe_target})
            add_dependencies(${exe_target} ascii-generate-defer-transformed-sources)
            target_compile_definitions(${exe_target} PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
        endif()
    endforeach()

    if(TARGET ascii-chat-shared)
        add_dependencies(ascii-chat-shared ascii-generate-defer-transformed-sources)
        target_compile_definitions(ascii-chat-shared PRIVATE ASCII_DEFER_TRANSFORMED_BUILD=1)
    endif()

endfunction()
