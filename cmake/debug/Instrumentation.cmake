include_guard(GLOBAL)

option(ASCII_BUILD_WITH_INSTRUMENTATION "Generate and build instrumented sources with per-statement logging" OFF)

include(${CMAKE_CURRENT_LIST_DIR}/Targets.cmake)
set(_ASCII_INSTRUMENTATION_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/run_instrumentation.sh")

function(_ascii_strip_source_include_dirs target_name)
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

function(_ascii_convert_path_for_shell input_path output_var)
    if(NOT input_path)
        set(${output_var} "" PARENT_SCOPE)
        return()
    endif()
    cmake_path(CONVERT "${input_path}" TO_CMAKE_PATH_LIST _ascii_posix_path)
    if(_ascii_posix_path MATCHES "^([A-Za-z]):/(.*)")
        string(TOLOWER "${CMAKE_MATCH_1}" _ascii_drive_letter)
        set(_ascii_result "/mnt/${_ascii_drive_letter}/${CMAKE_MATCH_2}")
    else()
        set(_ascii_result "${_ascii_posix_path}")
    endif()
    set(${output_var} "${_ascii_result}" PARENT_SCOPE)
endfunction()

set(ASCII_INSTRUMENTATION_LIBRARY_TARGETS
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

# ascii-instr-report uses POSIX headers - only available on Unix/Linux/macOS
if(WIN32)
    set(ASCII_INSTRUMENTATION_EXECUTABLE_TARGETS
        ascii-chat
    )
else()
    set(ASCII_INSTRUMENTATION_EXECUTABLE_TARGETS
        ascii-chat
        ascii-instr-report
    )
endif()

function(ascii_instrumentation_prepare)
    if(NOT ASCII_BUILD_WITH_INSTRUMENTATION)
        set(ASCII_INSTRUMENTATION_ENABLED FALSE PARENT_SCOPE)
        return()
    endif()

    ascii_add_debug_targets()

    set(USE_PRECOMPILED_HEADERS OFF CACHE BOOL "Disable PCH when instrumentation is enabled" FORCE)

    set(instrumented_dir "${CMAKE_BINARY_DIR}/instrumented")

    set(_ascii_instr_excluded_sources
        "lib/platform/system.c"
        "lib/platform/posix/system.c"
        "lib/platform/posix/mutex.c"
        "lib/platform/posix/thread.c"
        "lib/platform/windows/system.c"
        "lib/platform/windows/mutex.c"
        "lib/platform/windows/thread.c"
        "lib/debug/lock.c"
        # SIMD files use intrinsics that confuse the instrumentation tool
        "lib/image2ascii/simd/ascii_simd.c"
        "lib/image2ascii/simd/ascii_simd_color.c"
        "lib/image2ascii/simd/common.c"
        "lib/image2ascii/simd/avx2.c"
        "lib/image2ascii/simd/sse2.c"
        "lib/image2ascii/simd/ssse3.c"
        "lib/image2ascii/simd/neon.c"
        "lib/image2ascii/simd/sve.c"
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

    set(instrumented_abs_paths "")
    set(instrumented_rel_paths "")
    set(instrumented_generated_paths "")

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
            if(rel_path MATCHES "^lib/debug/")
                continue()
            endif()
            if(rel_path IN_LIST _ascii_instr_excluded_sources)
                continue()
            endif()

            set(generated_path "${instrumented_dir}/${rel_path}")
            list(APPEND instrumented_abs_paths "${abs_path}")
            list(APPEND instrumented_rel_paths "${rel_path}")
            list(APPEND instrumented_generated_paths "${generated_path}")
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES instrumented_abs_paths)
    list(REMOVE_DUPLICATES instrumented_rel_paths)
    list(REMOVE_DUPLICATES instrumented_generated_paths)

    if(DEFINED ASCII_INSTRUMENTATION_BASH AND ASCII_INSTRUMENTATION_BASH)
        set(_ascii_bash_executable "${ASCII_INSTRUMENTATION_BASH}")
    else()
        unset(_ascii_bash_executable CACHE)  # Clear any cached value
        if(WIN32)
            # Windows-specific search for Git Bash or WSL
            find_program(_ascii_bash_executable
                NAMES bash.exe bash
                HINTS
                    "$ENV{ProgramFiles}/Git/usr/bin"
                    "$ENV{ProgramFiles}/Git/bin"
                    "$ENV{ProgramFiles}/Git/cmd"
                    "$ENV{ProgramW6432}/Git/usr/bin"
                    "$ENV{ProgramW6432}/Git/bin"
                    "$ENV{ProgramW6432}/Git/cmd"
                    "$ENV{LOCALAPPDATA}/Programs/Git/usr/bin"
                    "$ENV{LOCALAPPDATA}/Programs/Git/bin"
                    "$ENV{LOCALAPPDATA}/Programs/Git/cmd"
                PATHS
                    "$ENV{SystemRoot}/system32"
                    "$ENV{SystemRoot}"
            )
            if(NOT _ascii_bash_executable)
                set(_ascii_candidate_bash_paths
                    "$ENV{SystemRoot}/system32/bash.exe"
                    "$ENV{ProgramFiles}/Git/usr/bin/bash.exe"
                    "$ENV{ProgramFiles}/Git/bin/bash.exe"
                    "$ENV{ProgramW6432}/Git/usr/bin/bash.exe"
                    "$ENV{ProgramW6432}/Git/bin/bash.exe"
                    "$ENV{LOCALAPPDATA}/Programs/Git/usr/bin/bash.exe"
                    "$ENV{LOCALAPPDATA}/Programs/Git/bin/bash.exe"
                )
                foreach(_ascii_candidate_path IN LISTS _ascii_candidate_bash_paths)
                    if(_ascii_candidate_path AND EXISTS "${_ascii_candidate_path}")
                        set(_ascii_bash_executable "${_ascii_candidate_path}")
                        break()
                    endif()
                endforeach()
            endif()
        else()
            # Unix (Linux/macOS) - use standard find_program with system PATH
            find_program(_ascii_bash_executable NAMES bash)
        endif()
    endif()

    if(NOT _ascii_bash_executable)
        message(FATAL_ERROR "ascii-chat instrumentation requires 'bash'. Install Git Bash or provide a path via ASCII_INSTRUMENTATION_BASH.")
    endif()

    set(_ascii_bash_uses_wsl FALSE)
    if(WIN32 AND _ascii_bash_executable MATCHES ".*[Ss]ystem32[/\\\\]bash\\.exe$")
        set(_ascii_bash_uses_wsl TRUE)
    endif()

    set(_ascii_instr_script_for_shell "${_ASCII_INSTRUMENTATION_SCRIPT}")
    set(_ascii_binary_dir_for_shell "${CMAKE_BINARY_DIR}")
    set(_ascii_instrumented_dir_for_shell "${instrumented_dir}")

    if(_ascii_bash_uses_wsl)
        _ascii_convert_path_for_shell("${_ascii_instr_script_for_shell}" _ascii_instr_script_for_shell)
        _ascii_convert_path_for_shell("${_ascii_binary_dir_for_shell}" _ascii_binary_dir_for_shell)
        _ascii_convert_path_for_shell("${_ascii_instrumented_dir_for_shell}" _ascii_instrumented_dir_for_shell)
    endif()

    # Generate compilation database with original source paths for instrumentation tool
    # The regular compile_commands.json has instrumented paths, but the tool needs original paths
    set(_ascii_temp_build_dir "${CMAKE_BINARY_DIR}/compile_db_temp")
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/compile_commands_original.json"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_ascii_temp_build_dir}"
        COMMAND ${CMAKE_COMMAND} -G Ninja
            -S "${CMAKE_SOURCE_DIR}"
            -B "${_ascii_temp_build_dir}"
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_RC_COMPILER=CMAKE_RC_COMPILER-NOTFOUND
            -DASCII_BUILD_WITH_INSTRUMENTATION=OFF
            -DUSE_PRECOMPILED_HEADERS=OFF
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        COMMAND ${CMAKE_COMMAND} --build "${_ascii_temp_build_dir}" --target generate_version
        COMMAND ${CMAKE_COMMAND} -E copy
            "${_ascii_temp_build_dir}/compile_commands.json"
            "${CMAKE_BINARY_DIR}/compile_commands_original.json"
        COMMENT "Generating compilation database for instrumentation tool"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${instrumented_dir}/.stamp"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${instrumented_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${instrumented_dir}"
        COMMAND ${CMAKE_COMMAND} -E env
            ASCII_INSTR_TOOL=$<TARGET_FILE:ascii-instr-tool>
            "${_ascii_bash_executable}" "${_ascii_instr_script_for_shell}" -b "${_ascii_binary_dir_for_shell}" -o "${_ascii_instrumented_dir_for_shell}"
        COMMAND ${CMAKE_COMMAND} -E touch "${instrumented_dir}/.stamp"
        DEPENDS ascii-instr-tool ${instrumented_abs_paths} "${CMAKE_BINARY_DIR}/compile_commands_original.json"
        BYPRODUCTS ${instrumented_generated_paths}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating instrumented source tree"
        VERBATIM
    )

    add_custom_target(ascii-generate-instrumented-sources
        DEPENDS "${instrumented_dir}/.stamp"
    )

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
            if(rel_path IN_LIST instrumented_rel_paths)
                # Instrumented source file
                set(generated_path "${instrumented_dir}/${rel_path}")
                list(APPEND updated_list "${generated_path}")
                set_source_files_properties("${generated_path}" PROPERTIES GENERATED TRUE)
            elseif(rel_path IN_LIST _ascii_instr_excluded_sources)
                # Excluded source file (copied to instrumented dir, not instrumented)
                set(generated_path "${instrumented_dir}/${rel_path}")
                list(APPEND updated_list "${generated_path}")
                set_source_files_properties("${generated_path}" PROPERTIES GENERATED TRUE)
            else()
                # Other files (deps, etc.) - use original
                list(APPEND updated_list "${item}")
            endif()
        endforeach()
        set(${var} "${updated_list}" PARENT_SCOPE)
    endforeach()

    set(ASCII_INSTRUMENTATION_ENABLED TRUE PARENT_SCOPE)
    set(ASCII_INSTRUMENTATION_SOURCE_DIR "${instrumented_dir}" PARENT_SCOPE)
endfunction()

function(ascii_instrumentation_finalize)
    if(NOT ASCII_BUILD_WITH_INSTRUMENTATION)
        return()
    endif()
    if(NOT TARGET ascii-generate-instrumented-sources)
        return()
    endif()

    set(instrumented_dir "${CMAKE_BINARY_DIR}/instrumented")

    set(instrumented_include_dirs
        "${instrumented_dir}/lib"
        "${instrumented_dir}/src"
    )

    # Debug/instrumentation runtime targets that are built before instrumentation
    # need the original lib/ headers, not instrumented ones
    if(TARGET ascii-chat-debug)
        target_include_directories(ascii-chat-debug PRIVATE
            ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_SOURCE_DIR}/src
        )
    endif()
    if(TARGET ascii-debug-runtime AND NOT TARGET ascii-chat-debug)
        target_include_directories(ascii-debug-runtime PRIVATE
            ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_SOURCE_DIR}/src
        )
    endif()

    foreach(lib_target IN LISTS ASCII_INSTRUMENTATION_LIBRARY_TARGETS)
        if(TARGET ${lib_target})
            add_dependencies(${lib_target} ascii-generate-instrumented-sources)
            _ascii_strip_source_include_dirs(${lib_target})
            # Prepend instrumented include paths so instrumented headers are found first
            target_include_directories(${lib_target} BEFORE PRIVATE ${instrumented_include_dirs})
        endif()
    endforeach()

    foreach(exe_target IN LISTS ASCII_INSTRUMENTATION_EXECUTABLE_TARGETS)
        if(TARGET ${exe_target})
            if(NOT exe_target IN_LIST _ascii_skip_strip_targets)
                add_dependencies(${exe_target} ascii-generate-instrumented-sources)
                _ascii_strip_source_include_dirs(${exe_target})
                # Prepend instrumented include paths for executables too
                target_include_directories(${exe_target} BEFORE PRIVATE ${instrumented_include_dirs})
            endif()
            if(TARGET ascii-debug-runtime)
                # Use plain signature to match existing target_link_libraries usage
                target_link_libraries(${exe_target} ascii-debug-runtime)
            endif()
        endif()
    endforeach()

    if(TARGET ascii-chat-shared AND TARGET ascii-debug-runtime)
        add_dependencies(ascii-chat-shared ascii-generate-instrumented-sources)
        _ascii_strip_source_include_dirs(ascii-chat-shared)
        target_include_directories(ascii-chat-shared BEFORE PRIVATE ${instrumented_include_dirs})
        target_link_libraries(ascii-chat-shared PRIVATE ascii-debug-runtime)
    endif()

endfunction()
