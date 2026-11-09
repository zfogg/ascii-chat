include_guard(GLOBAL)

option(ASCIICHAT_BUILD_WITH_PANIC "Generate and build panic-instrumented sources with per-statement logging" OFF)
set(ASCIICHAT_PANIC_TOOL "" CACHE FILEPATH "Path to pre-built ascii-instr-panic tool (optional)")

include(${CMAKE_SOURCE_DIR}/cmake/utils/BuildLLVMTool.cmake)

function(ascii_panic_prepare)
    if(NOT ASCIICHAT_BUILD_WITH_PANIC)
        return()
    endif()
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/tooling/panic/tool.cpp" "${CMAKE_SOURCE_DIR}/src/tooling/panic/CMakeLists.txt")
    file(SHA256 "${CMAKE_SOURCE_DIR}/src/tooling/panic/tool.cpp" _tool_source_hash)
    file(SHA256 "${CMAKE_SOURCE_DIR}/src/tooling/panic/CMakeLists.txt" _tool_cmake_hash)
    string(SHA256 _cache_hash "${_tool_source_hash};${_tool_cmake_hash};${CMAKE_CXX_COMPILER};${CMAKE_HOST_SYSTEM_NAME};${CMAKE_HOST_SYSTEM_PROCESSOR}")
    string(SUBSTRING "${_cache_hash}" 0 16 _cache_id)
    build_llvm_tool(
        NAME panic
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/tooling/panic"
        CACHE_DIR_NAME "panic-tool-${_cache_id}"
        OUTPUT_EXECUTABLE "ascii-instr-panic"
        PREBUILT_VAR ASCIICHAT_PANIC_TOOL
        CLEAN_INCOMPLETE_CACHE
        CREATE_IMPORTED_TARGET "ascii-instr-panic"
    )
    execute_process(COMMAND "${CMAKE_C_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE _resource_dir OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0 OR NOT IS_DIRECTORY "${_resource_dir}")
        message(FATAL_ERROR "Cannot determine Clang resource directory for panic instrumentation")
    endif()
    set(ASCII_PANIC_RESOURCE_DIR "${_resource_dir}" PARENT_SCOPE)
    file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/include/*.h" "${CMAKE_SOURCE_DIR}/lib/*.h" "${CMAKE_SOURCE_DIR}/src/*.h")
    set(ASCII_PANIC_HEADERS "${_headers}" PARENT_SCOPE)
    if(CMAKE_GENERATOR STREQUAL "Ninja")
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS ascii_panic_pool=4)
    endif()
endfunction()

# Instrument each target's source with that target's compilation command. The
# output is fed into defer when necessary; the original remains untouched.
function(ascii_panic_instrument_source out_var target source relative compiled)
    set(${out_var} "${source}" PARENT_SCOPE)
    if(NOT ASCIICHAT_BUILD_WITH_PANIC OR NOT _batch_stage STREQUAL "production")
        return()
    endif()
    if(NOT target MATCHES "^ascii-chat(-|$)" OR target STREQUAL "ascii-chat-panic" OR
       relative MATCHES "^lib/(debug|video/simd)/" OR
       relative MATCHES "^lib/platform/(system\\.c|(posix|windows)/(system|mutex|thread)\\.c)$")
        return()
    endif()
    set(_root "${CMAKE_BINARY_DIR}/panic_transformed/${target}")
    set(_output "${_root}/${relative}")
    set(_pool "")
    if(CMAKE_GENERATOR STREQUAL "Ninja")
        set(_pool JOB_POOL ascii_panic_pool)
    endif()
    add_custom_command(OUTPUT "${_output}"
        COMMAND "$<TARGET_FILE:ascii-instr-panic>" -p "${CMAKE_BINARY_DIR}"
            --output-dir "${_root}" --input-root "${CMAKE_SOURCE_DIR}"
            --compile-source "${compiled}" --resource-dir "${ASCII_PANIC_RESOURCE_DIR}" "${source}"
        DEPENDS "${source}" ${ASCII_PANIC_HEADERS} ascii-instr-panic "$<TARGET_FILE:ascii-instr-panic>" "${CMAKE_BINARY_DIR}/compile_commands.json"
        ${_pool} COMMENT "Instrumenting ${relative} for panic diagnostics" VERBATIM)
    # The target can be declared in a different CMake directory from this
    # command. An explicit custom target carries the generating rule across
    # that directory boundary.
    string(MD5 _id "${target}|${relative}")
    add_custom_target("ascii-panic-source-${_id}" DEPENDS "${_output}")
    add_dependencies("${target}" "ascii-panic-source-${_id}")
    set_source_files_properties("${_output}" TARGET_DIRECTORY "${target}" PROPERTIES GENERATED TRUE)
    set(${out_var} "${_output}" PARENT_SCOPE)
endfunction()

function(ascii_panic_finalize)
    if(NOT ASCIICHAT_BUILD_WITH_PANIC)
        return()
    endif()
    if(TARGET ascii-chat AND TARGET ascii-panic-runtime)
        target_link_libraries(ascii-chat PRIVATE ascii-panic-runtime)
    endif()
endfunction()
