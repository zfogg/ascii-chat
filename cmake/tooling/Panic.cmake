include_guard(GLOBAL)

option(ASCIICHAT_BUILD_WITH_PANIC "Generate and build panic-instrumented sources with per-statement logging" OFF)

# Path to pre-built panic tool (optional, for CI or cross-compilation)
set(ASCIICHAT_PANIC_TOOL "" CACHE FILEPATH "Path to pre-built ascii-instr-panic tool (optional)")

include(${CMAKE_SOURCE_DIR}/cmake/utils/BuildLLVMTool.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/utils/GenerateCompilationDB.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/utils/PathConversion.cmake)
set(_ASCII_PANIC_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/tooling/run_panic_instrumentation.sh")

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

# Note: Path conversion function moved to cmake/utils/PathConversion.cmake
# Use convert_path_for_shell() and is_wsl_bash() from that module

set(ASCII_PANIC_LIBRARY_TARGETS
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

# ascii-panic-report uses POSIX headers - only available on Unix/Linux/macOS
if(WIN32)
    set(ASCII_PANIC_EXECUTABLE_TARGETS
        ascii-chat
    )
else()
    set(ASCII_PANIC_EXECUTABLE_TARGETS
        ascii-chat
        ascii-panic-report
    )
endif()

function(ascii_panic_prepare)
    if(NOT ASCIICHAT_BUILD_WITH_PANIC)
        set(ASCII_PANIC_ENABLED FALSE PARENT_SCOPE)
        return()
    endif()

    # =========================================================================
    # Build/cache the panic tool using the common utility
    # =========================================================================
    build_llvm_tool(
        NAME panic
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/tooling/panic"
        CACHE_DIR_NAME "panic-tool"
        OUTPUT_EXECUTABLE "ascii-instr-panic"
        PREBUILT_VAR ASCIICHAT_PANIC_TOOL
        CLEAN_INCOMPLETE_CACHE
        CREATE_IMPORTED_TARGET "ascii-instr-panic"
    )

    set(_panic_tool_exe "${PANIC_TOOL_EXECUTABLE}")
    set(_panic_tool_depends "${PANIC_TOOL_DEPENDS}")

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
        # SIMD files use intrinsics that confuse the panic instrumentation tool
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

            # Handle defer-transformed sources: strip build*/defer_transformed/ prefix
            # When defer runs first, sources are in build_panic/defer_transformed/lib/...
            # We need to compute the output path relative to the original source structure
            if(rel_path MATCHES "^build[^/]*/defer_transformed/(.*)")
                set(rel_path "${CMAKE_MATCH_1}")
            endif()

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

    # Use centralized bash from FindPrograms.cmake (or user override)
    if(DEFINED ASCII_PANIC_BASH AND ASCII_PANIC_BASH)
        set(_ascii_bash_executable "${ASCII_PANIC_BASH}")
    elseif(ASCIICHAT_BASH_EXECUTABLE)
        set(_ascii_bash_executable "${ASCIICHAT_BASH_EXECUTABLE}")
    else()
        message(FATAL_ERROR "ascii-chat panic requires 'bash'. Install Git Bash or provide a path via ASCII_PANIC_BASH.")
    endif()

    # Check if bash is WSL and convert paths if needed
    is_wsl_bash("${_ascii_bash_executable}" _ascii_bash_uses_wsl)

    set(_ascii_instr_script_for_shell "${_ASCII_PANIC_SCRIPT}")
    set(_ascii_binary_dir_for_shell "${CMAKE_BINARY_DIR}")
    set(_ascii_instrumented_dir_for_shell "${instrumented_dir}")

    if(_ascii_bash_uses_wsl)
        convert_path_for_shell("${_ascii_instr_script_for_shell}" _ascii_instr_script_for_shell)
        convert_path_for_shell("${_ascii_binary_dir_for_shell}" _ascii_binary_dir_for_shell)
        convert_path_for_shell("${_ascii_instrumented_dir_for_shell}" _ascii_instrumented_dir_for_shell)
    endif()

    # Detect Clang resource directory for compilation database generation
    # This is required for Clang's LibTooling to find builtin headers like stdatomic.h
    detect_clang_resource_dir(_panic_clang_resource_dir)
    if(_panic_clang_resource_dir)
        message(STATUS "Panic tool: Using Clang resource directory: ${_panic_clang_resource_dir}")
    else()
        message(WARNING "Panic tool: Could not detect Clang resource directory - builtin headers may not be found")
    endif()

    # Generate compilation database with original source paths using the common utility
    set(_ascii_temp_build_dir "${CMAKE_BINARY_DIR}/compile_db_temp")
    generate_compilation_database(
        OUTPUT "${CMAKE_BINARY_DIR}/compile_commands_original.json"
        TEMP_DIR "${_ascii_temp_build_dir}"
        LOG_FILE "${CMAKE_BINARY_DIR}/panic_compile_db.log"
        COMMENT "Generating compilation database for the panic tool"
        CLANG_RESOURCE_DIR "${_panic_clang_resource_dir}"
        DISABLE_OPTIONS ASCIICHAT_BUILD_WITH_PANIC ASCIICHAT_USE_PCH
    )

    add_custom_command(
        OUTPUT "${instrumented_dir}/.stamp"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${instrumented_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${instrumented_dir}"
        COMMAND ${CMAKE_COMMAND} -E env
            ASCII_PANIC_TOOL=$<TARGET_FILE:ascii-instr-panic>
            "${_ascii_bash_executable}" "${_ascii_instr_script_for_shell}" -b "${_ascii_binary_dir_for_shell}" -o "${_ascii_instrumented_dir_for_shell}"
        COMMAND ${CMAKE_COMMAND} -E touch "${instrumented_dir}/.stamp"
        DEPENDS ascii-instr-panic ${instrumented_abs_paths} "${CMAKE_BINARY_DIR}/compile_commands_original.json"
        BYPRODUCTS ${instrumented_generated_paths}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating panic-instrumented source tree"
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
            # Handle defer-transformed sources: strip build*/defer_transformed/ prefix
            if(rel_path MATCHES "^build[^/]*/defer_transformed/(.*)")
                set(rel_path "${CMAKE_MATCH_1}")
            endif()
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

    set(ASCII_PANIC_ENABLED TRUE PARENT_SCOPE)
    set(ASCII_PANIC_SOURCE_DIR "${instrumented_dir}" PARENT_SCOPE)
endfunction()

function(ascii_panic_finalize)
    if(NOT ASCIICHAT_BUILD_WITH_PANIC)
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

    # Debug/panic runtime targets that are built before instrumentation
    # need the original lib/ headers, not instrumented ones
    if(TARGET ascii-chat-panic)
        target_include_directories(ascii-chat-panic PRIVATE
            ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_SOURCE_DIR}/src
        )
    endif()
    if(TARGET ascii-panic-runtime AND NOT TARGET ascii-chat-panic)
        target_include_directories(ascii-panic-runtime PRIVATE
            ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_SOURCE_DIR}/src
        )
    endif()

    foreach(lib_target IN LISTS ASCII_PANIC_LIBRARY_TARGETS)
        if(TARGET ${lib_target})
            add_dependencies(${lib_target} ascii-generate-instrumented-sources)
            _ascii_strip_source_include_dirs(${lib_target})
            # Prepend instrumented include paths so instrumented headers are found first
            target_include_directories(${lib_target} BEFORE PRIVATE ${instrumented_include_dirs})
        endif()
    endforeach()

    foreach(exe_target IN LISTS ASCII_PANIC_EXECUTABLE_TARGETS)
        if(TARGET ${exe_target})
            if(NOT exe_target IN_LIST _ascii_skip_strip_targets)
                add_dependencies(${exe_target} ascii-generate-instrumented-sources)
                _ascii_strip_source_include_dirs(${exe_target})
                # Prepend instrumented include paths for executables too
                target_include_directories(${exe_target} BEFORE PRIVATE ${instrumented_include_dirs})
            endif()
            if(TARGET ascii-panic-runtime)
                # Use plain signature to match existing target_link_libraries usage
                target_link_libraries(${exe_target} ascii-panic-runtime)
            endif()
        endif()
    endforeach()

    if(TARGET ascii-chat-shared AND TARGET ascii-panic-runtime)
        add_dependencies(ascii-chat-shared ascii-generate-instrumented-sources)
        _ascii_strip_source_include_dirs(ascii-chat-shared)
        target_include_directories(ascii-chat-shared BEFORE PRIVATE ${instrumented_include_dirs})
        target_link_libraries(ascii-chat-shared PRIVATE ascii-panic-runtime)
    endif()

endfunction()
