include_guard(GLOBAL)

# Parent targets share one lock because they all write the same nested Ninja state.
function(asciichat_add_wasm_targets WASM_BUILD_DIR)
    foreach(_target mirror-web client-web web)
        if(TARGET ${_target})
            continue()
        endif()
        if(_target STREQUAL "web")
            set(_nested_targets mirror-web client-web)
        else()
            set(_nested_targets ${_target})
        endif()
        add_custom_target(${_target}
            COMMAND "${CMAKE_COMMAND}"
                "-DWASM_BUILD_DIR=${WASM_BUILD_DIR}"
                "-DWASM_TARGETS=${_nested_targets}"
                "-DWASM_CONFIG=$<CONFIG>"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/build_wasm.cmake"
            COMMENT "Building WASM target ${_target}"
            USES_TERMINAL
            VERBATIM)
        if(TARGET generate_fonts)
            add_dependencies(${_target} generate_fonts)
        endif()
    endforeach()
endfunction()
