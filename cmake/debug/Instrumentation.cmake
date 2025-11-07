include_guard(GLOBAL)

option(ASCII_BUILD_WITH_INSTRUMENTATION "Generate and build instrumented sources with per-statement logging" OFF)

include(${CMAKE_CURRENT_LIST_DIR}/Targets.cmake)
set(_ASCII_INSTRUMENTATION_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/run_instrumentation.sh")

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

set(ASCII_INSTRUMENTATION_EXECUTABLE_TARGETS
    ascii-chat
    ascii-chat-client
    ascii-chat-server
    ascii-instr-report
)

function(ascii_instrumentation_prepare)
    if(NOT ASCII_BUILD_WITH_INSTRUMENTATION)
        set(ASCII_INSTRUMENTATION_ENABLED FALSE PARENT_SCOPE)
        return()
    endif()

    ascii_add_debug_targets()

    set(USE_PRECOMPILED_HEADERS OFF CACHE BOOL "Disable PCH when instrumentation is enabled" FORCE)

    set(instrumented_dir "${CMAKE_BINARY_DIR}/instrumented")

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

            set(generated_path "${instrumented_dir}/${rel_path}")
            list(APPEND instrumented_abs_paths "${abs_path}")
            list(APPEND instrumented_rel_paths "${rel_path}")
            list(APPEND instrumented_generated_paths "${generated_path}")
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES instrumented_abs_paths)
    list(REMOVE_DUPLICATES instrumented_rel_paths)
    list(REMOVE_DUPLICATES instrumented_generated_paths)

    add_custom_command(
        OUTPUT "${instrumented_dir}/.stamp"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${instrumented_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${instrumented_dir}"
        COMMAND ${CMAKE_COMMAND} -E env
            ASCII_INSTR_TOOL=$<TARGET_FILE:ascii-instr-tool>
            bash "${_ASCII_INSTRUMENTATION_SCRIPT}" -b "${CMAKE_BINARY_DIR}" -o "${instrumented_dir}"
        COMMAND ${CMAKE_COMMAND} -E touch "${instrumented_dir}/.stamp"
        DEPENDS ascii-instr-tool ${instrumented_abs_paths}
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
                set(generated_path "${instrumented_dir}/${rel_path}")
                list(APPEND updated_list "${generated_path}")
                set_source_files_properties("${generated_path}" PROPERTIES GENERATED TRUE)
            else()
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

    foreach(lib_target IN LISTS ASCII_INSTRUMENTATION_LIBRARY_TARGETS)
        if(TARGET ${lib_target})
            add_dependencies(${lib_target} ascii-generate-instrumented-sources)
        endif()
    endforeach()

    foreach(exe_target IN LISTS ASCII_INSTRUMENTATION_EXECUTABLE_TARGETS)
        if(TARGET ${exe_target})
            add_dependencies(${exe_target} ascii-generate-instrumented-sources)
            if(TARGET ascii-debug-runtime)
                # Use plain signature to match existing target_link_libraries usage
                target_link_libraries(${exe_target} ascii-debug-runtime)
            endif()
        endif()
    endforeach()

    if(TARGET ascii-chat-shared AND TARGET ascii-debug-runtime)
        add_dependencies(ascii-chat-shared ascii-generate-instrumented-sources)
        target_link_libraries(ascii-chat-shared PRIVATE ascii-debug-runtime)
    endif()

endfunction()
