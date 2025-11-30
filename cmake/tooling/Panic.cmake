include_guard(GLOBAL)

option(ASCIICHAT_BUILD_WITH_PANIC "Generate and build panic-instrumented sources with per-statement logging" OFF)

# Path to pre-built panic tool (optional, for CI or cross-compilation)
set(ASCIICHAT_PANIC_TOOL "" CACHE FILEPATH "Path to pre-built ascii-instr-panic tool (optional)")

include(ExternalProject)
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
    # Build/cache the panic tool as an external project
    # =========================================================================
    # Priority: 1. ASCIICHAT_PANIC_TOOL (explicit), 2. Cached build, 3. Build new
    set(_panic_cache_dir "${CMAKE_SOURCE_DIR}/.deps-cache/panic-tool")
    set(_panic_cached_exe "${_panic_cache_dir}/ascii-instr-panic${CMAKE_EXECUTABLE_SUFFIX}")

    # Clean up any partial cache state BEFORE checking if exe exists
    # This handles CI cache restore that has incomplete state (directory exists,
    # maybe CMakeCache.txt, but no exe) which causes "not a CMake build directory" errors
    set(_panic_stamp_dir "${CMAKE_BINARY_DIR}/ascii-instr-panic-external-prefix/src/ascii-instr-panic-external-stamp")
    set(_panic_needs_rebuild FALSE)

    if(EXISTS "${_panic_cache_dir}")
        if(NOT EXISTS "${_panic_cache_dir}/CMakeCache.txt")
            message(STATUS "Cleaning incomplete panic tool cache (no CMakeCache.txt): ${_panic_cache_dir}")
            file(REMOVE_RECURSE "${_panic_cache_dir}")
            set(_panic_needs_rebuild TRUE)
        elseif(NOT EXISTS "${_panic_cached_exe}")
            message(STATUS "Cleaning incomplete panic tool cache (no exe): ${_panic_cache_dir}")
            file(REMOVE_RECURSE "${_panic_cache_dir}")
            set(_panic_needs_rebuild TRUE)
        endif()
    else()
        # Cache directory doesn't exist at all - this can happen when:
        # 1. CI cache only restored build/ but not .deps-cache/
        # 2. Fresh checkout
        # In either case, we need to clean any stale stamp files
        set(_panic_needs_rebuild TRUE)
    endif()

    # Also clean up stale stamp files if we need a rebuild
    if(_panic_needs_rebuild AND EXISTS "${_panic_stamp_dir}")
        message(STATUS "Cleaning stale panic tool stamp files: ${_panic_stamp_dir}")
        file(REMOVE_RECURSE "${_panic_stamp_dir}")
    endif()

    if(ASCIICHAT_PANIC_TOOL AND EXISTS "${ASCIICHAT_PANIC_TOOL}")
        set(_panic_tool_exe "${ASCIICHAT_PANIC_TOOL}")
        set(_panic_tool_depends "")
        message(STATUS "Using external panic tool: ${_panic_tool_exe}")
    elseif(EXISTS "${_panic_cached_exe}" AND EXISTS "${_panic_cache_dir}/CMakeCache.txt")
        # Use cached panic tool (persists across build directory deletes)
        # Both exe AND CMakeCache.txt must exist for a valid cache
        set(_panic_tool_exe "${_panic_cached_exe}")
        set(_panic_tool_depends "")
        message(STATUS "Using cached panic tool: ${_panic_tool_exe}")
    else()
        # Build panic tool to cache directory (survives rm -rf build)
        set(_panic_build_dir "${_panic_cache_dir}")
        set(_panic_tool_exe "${_panic_build_dir}/ascii-instr-panic${CMAKE_EXECUTABLE_SUFFIX}")

        # Detect the C++ compiler for building the panic tool
        # On macOS, we MUST use Apple's system clang for compiling the panic tool
        # because Homebrew LLVM's libc++ headers are incompatible with -nostdinc.
        # The panic tool links against Homebrew LLVM libraries but compiles with system clang.
        if(APPLE AND EXISTS "/usr/bin/clang++")
            set(_panic_cxx_compiler "/usr/bin/clang++")
            message(STATUS "Panic tool: Using Apple system clang for compilation: ${_panic_cxx_compiler}")
        elseif(CMAKE_CXX_COMPILER)
            set(_panic_cxx_compiler "${CMAKE_CXX_COMPILER}")
        elseif(CMAKE_C_COMPILER MATCHES "clang")
            get_filename_component(_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
            find_program(_panic_cxx_compiler NAMES clang++ PATHS "${_compiler_dir}" NO_DEFAULT_PATH)
            if(NOT _panic_cxx_compiler)
                find_program(_panic_cxx_compiler NAMES clang++)
            endif()
        else()
            find_program(_panic_cxx_compiler NAMES c++ g++)
        endif()

        # Configure cmake args for the external project
        set(_panic_cmake_args
            -DCMAKE_CXX_COMPILER=${_panic_cxx_compiler}
            -DCMAKE_BUILD_TYPE=Release
            -DOUTPUT_DIR=${_panic_build_dir}
        )

        # On Windows with vcpkg, pass the vcpkg root so the tool can find dependencies
        if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
            list(APPEND _panic_cmake_args
                -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
                -DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}
            )
            # Force empty linker flags to prevent -nostartfiles -nostdlib
            set(_panic_cache_args
                -DCMAKE_CXX_COMPILER_WORKS:BOOL=TRUE
                -DCMAKE_C_COMPILER_WORKS:BOOL=TRUE
                -DCMAKE_EXE_LINKER_FLAGS:STRING=
                -DCMAKE_EXE_LINKER_FLAGS_DEBUG:STRING=
                -DCMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=
                -DCMAKE_SHARED_LINKER_FLAGS:STRING=
                -DCMAKE_MODULE_LINKER_FLAGS:STRING=
                -DCMAKE_PLATFORM_REQUIRED_RUNTIME_PATH:STRING=
            )
        else()
            set(_panic_cache_args "")
        endif()

        # Verify that the panic tool CMakeLists.txt exists
        set(_panic_source_dir "${CMAKE_SOURCE_DIR}/src/tooling/panic")
        set(_panic_cmake_file "${_panic_source_dir}/CMakeLists.txt")
        if(NOT EXISTS "${_panic_cmake_file}")
            message(FATAL_ERROR
                "ERROR: Panic tool CMakeLists.txt not found at: ${_panic_cmake_file}\n"
                "The panic tool requires a CMakeLists.txt file in src/tooling/panic/ to build.\n"
                "Please ensure src/tooling/panic/CMakeLists.txt exists in the repository."
            )
        endif()

        ExternalProject_Add(ascii-instr-panic-external
            SOURCE_DIR "${_panic_source_dir}"
            BINARY_DIR "${_panic_build_dir}"
            CMAKE_ARGS ${_panic_cmake_args}
            CMAKE_CACHE_ARGS ${_panic_cache_args}
            BUILD_ALWAYS FALSE
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${_panic_tool_exe}"
        )

        set(_panic_tool_depends ascii-instr-panic-external)
        message(STATUS "Building panic tool to cache: ${_panic_cache_dir}")
        message(STATUS "  (To force rebuild, delete: ${_panic_cached_exe})")
    endif()

    # Create an imported target for the panic tool so DEPENDS can use it
    if(NOT TARGET ascii-instr-panic)
        add_executable(ascii-instr-panic IMPORTED GLOBAL)
        set_target_properties(ascii-instr-panic PROPERTIES
            IMPORTED_LOCATION "${_panic_tool_exe}"
        )
        if(_panic_tool_depends)
            add_dependencies(ascii-instr-panic ${_panic_tool_depends})
        endif()
    endif()

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

            # Handle defer-transformed sources: strip build/defer_transformed/ prefix
            # When defer runs first, sources are in build/defer_transformed/lib/...
            # We need to compute the output path relative to the original source structure
            if(rel_path MATCHES "^build/defer_transformed/(.*)")
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

    if(DEFINED ASCII_PANIC_BASH AND ASCII_PANIC_BASH)
        set(_ascii_bash_executable "${ASCII_PANIC_BASH}")
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
        message(FATAL_ERROR "ascii-chat panic requires 'bash'. Install Git Bash or provide a path via ASCII_PANIC_BASH.")
    endif()

    set(_ascii_bash_uses_wsl FALSE)
    if(WIN32 AND _ascii_bash_executable MATCHES ".*[Ss]ystem32[/\\\\]bash\\.exe$")
        set(_ascii_bash_uses_wsl TRUE)
    endif()

    set(_ascii_instr_script_for_shell "${_ASCII_PANIC_SCRIPT}")
    set(_ascii_binary_dir_for_shell "${CMAKE_BINARY_DIR}")
    set(_ascii_instrumented_dir_for_shell "${instrumented_dir}")

    if(_ascii_bash_uses_wsl)
        _ascii_convert_path_for_shell("${_ascii_instr_script_for_shell}" _ascii_instr_script_for_shell)
        _ascii_convert_path_for_shell("${_ascii_binary_dir_for_shell}" _ascii_binary_dir_for_shell)
        _ascii_convert_path_for_shell("${_ascii_instrumented_dir_for_shell}" _ascii_instrumented_dir_for_shell)
    endif()

    # Generate compilation database with original source paths for the panic instrumentation tool
    # The regular compile_commands.json has instrumented paths, but the panic tool needs the originals
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
            -DASCIICHAT_BUILD_WITH_PANIC=OFF
            -DASCIICHAT_USE_PCH=OFF
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        COMMAND ${CMAKE_COMMAND} --build "${_ascii_temp_build_dir}" --target generate_version
        COMMAND ${CMAKE_COMMAND} -E copy
            "${_ascii_temp_build_dir}/compile_commands.json"
            "${CMAKE_BINARY_DIR}/compile_commands_original.json"
        COMMENT "Generating compilation database for the panic tool"
        VERBATIM
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
            # Handle defer-transformed sources: strip build/defer_transformed/ prefix
            if(rel_path MATCHES "^build/defer_transformed/(.*)")
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
