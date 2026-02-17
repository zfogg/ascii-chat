include_guard(GLOBAL)

# Query Tool - LLDB-based runtime variable inspection
#
# Unlike defer/panic tools which do source transformation, the query tool is
# an external debugger that attaches to the running process via LLDB.
#
# Components:
#   1. ascii-query-server   - Controller executable (uses LLDB SB API + HTTP server)
#   2. ascii-query-runtime  - Runtime library for auto-spawn (QUERY_INIT/SHUTDOWN macros)
#
# Build the query tool as an external project to avoid inheriting musl/static flags

set(ASCIICHAT_QUERY_TOOL "" CACHE FILEPATH "Path to pre-built ascii-query-server tool (optional)")

option(ASCIICHAT_BUILD_WITH_QUERY "Build query debug tool (requires LLDB)" OFF)

include(${CMAKE_SOURCE_DIR}/cmake/utils/BuildLLVMTool.cmake)

function(ascii_query_prepare)
    if(NOT ASCIICHAT_BUILD_WITH_QUERY)
        message(STATUS "Query tool disabled (ASCIICHAT_BUILD_WITH_QUERY=OFF)")
        set(ASCII_QUERY_ENABLED FALSE PARENT_SCOPE)
        return()
    endif()

    # Query tool only makes sense in Debug builds (needs debug symbols)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT CMAKE_BUILD_TYPE STREQUAL "Dev")
        message(STATUS "Query tool disabled (requires Debug or Dev build type, got ${CMAKE_BUILD_TYPE})")
        set(ASCII_QUERY_ENABLED FALSE PARENT_SCOPE)
        return()
    endif()

    # Build/find the query tool using the common utility
    build_llvm_tool(
        NAME query
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/tooling/query"
        CACHE_DIR_NAME "query-tool"
        OUTPUT_EXECUTABLE "ascii-query-server"
        PREBUILT_VAR ASCIICHAT_QUERY_TOOL
        CLEAN_INCOMPLETE_CACHE
        EXTRA_CMAKE_ARGS
            "-DASCIICHAT_INCLUDE_DIR=${CMAKE_SOURCE_DIR}/include"
    )

    # Store the tool path for later use
    set(ASCII_QUERY_TOOL_EXE "${QUERY_TOOL_EXECUTABLE}" PARENT_SCOPE)
    set(ASCII_QUERY_TOOL_DEPENDS "${QUERY_TOOL_DEPENDS}" PARENT_SCOPE)
    set(ASCII_QUERY_ENABLED TRUE PARENT_SCOPE)

    # Build the runtime library (for QUERY_INIT/SHUTDOWN macros)
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/tooling/query ${CMAKE_BINARY_DIR}/lib/tooling/query)

    message(STATUS "Query tool enabled")
    message(STATUS "  Controller: ${_query_tool_exe}")
    message(STATUS "  Runtime: ascii-query-runtime (INTERFACE library)")
endfunction()

function(ascii_query_finalize)
    if(NOT ASCII_QUERY_ENABLED)
        return()
    endif()

    # Define targets that should link against query runtime
    set(ASCII_QUERY_LINK_TARGETS
        ascii-chat
    )

    # Link runtime library and add compile definitions
    # Note: Use plain signature (no PRIVATE) to match existing ascii-chat link style
    foreach(target IN LISTS ASCII_QUERY_LINK_TARGETS)
        if(TARGET ${target})
            target_link_libraries(${target} ascii-query-runtime)
            target_compile_definitions(${target} PRIVATE ASCIICHAT_QUERY_ENABLED=1)

            # Ensure query tool is built before main app (if building from source)
            if(ASCII_QUERY_TOOL_DEPENDS)
                add_dependencies(${target} ${ASCII_QUERY_TOOL_DEPENDS})
            endif()
        endif()
    endforeach()

    # Also handle shared library if it exists
    # Note: ascii-chat-shared uses keyword signature (PRIVATE), unlike ascii-chat
    if(TARGET ascii-chat-shared)
        target_link_libraries(ascii-chat-shared PRIVATE ascii-query-runtime)
        target_compile_definitions(ascii-chat-shared PRIVATE ASCIICHAT_QUERY_ENABLED=1)
        if(ASCII_QUERY_TOOL_DEPENDS)
            add_dependencies(ascii-chat-shared ${ASCII_QUERY_TOOL_DEPENDS})
        endif()
    endif()

    # Copy query tool to bin directory for convenience
    if(ASCII_QUERY_TOOL_EXE)
        set(_query_bin_dest "${CMAKE_BINARY_DIR}/bin/ascii-query-server${CMAKE_EXECUTABLE_SUFFIX}")
        add_custom_command(
            OUTPUT "${_query_bin_dest}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ASCII_QUERY_TOOL_EXE}"
                "${_query_bin_dest}"
            DEPENDS ${ASCII_QUERY_TOOL_DEPENDS} "${ASCII_QUERY_TOOL_EXE}"
            COMMENT "Copying ascii-query-server to bin/"
            VERBATIM
        )
        add_custom_target(ascii-query-server-copy ALL
            DEPENDS "${_query_bin_dest}"
        )

        # On Windows, copy liblldb.dll and its dependencies to bin directory
        if(WIN32)
            include(${CMAKE_SOURCE_DIR}/cmake/utils/CopyDLL.cmake)

            # Use LLVM_ROOT_PREFIX from LLVM.cmake (single source of truth)
            if(LLVM_ROOT_PREFIX)
                set(_llvm_bindir "${LLVM_ROOT_PREFIX}/bin")
                message(STATUS "Query tool: Using LLVM bindir from LLVM_ROOT_PREFIX: ${_llvm_bindir}")
            else()
                message(WARNING "Query tool: LLVM_ROOT_PREFIX not set, cannot find liblldb.dll")
                set(_llvm_bindir "")
            endif()

            if(_llvm_bindir)
                # liblldb.dll from LLVM install
                copy_dll(
                    NAME liblldb.dll
                    HINTS "${_llvm_bindir}"
                    DEST "${CMAKE_BINARY_DIR}/bin"
                    COMMENT "for query tool"
                    DEPENDS_TARGET ascii-query-server-copy
                )

                # LLDB dependency DLLs from vcpkg
                if(DEFINED VCPKG_INSTALLED_DIR)
                    set(_vcpkg_bin "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
                    copy_dlls(
                        NAMES liblzma.dll libxml2.dll zlib1.dll zstd.dll iconv-2.dll
                        HINTS "${_vcpkg_bin}"
                        DEST "${CMAKE_BINARY_DIR}/bin"
                        COMMENT "for query tool"
                        DEPENDS_TARGET ascii-query-server-copy
                    )
                endif()

                # Python DLL
                find_package(Python3 COMPONENTS Interpreter)
                if(Python3_FOUND AND Python3_EXECUTABLE)
                    get_filename_component(_python_dir "${Python3_EXECUTABLE}" DIRECTORY)
                    string(REPLACE "." "" _python_ver "${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}")
                    copy_dll(
                        NAME "python${_python_ver}.dll"
                        HINTS "${_python_dir}"
                        DEST "${CMAKE_BINARY_DIR}/bin"
                        COMMENT "for query tool"
                        DEPENDS_TARGET ascii-query-server-copy
                    )
                endif()
            endif()
        endif()
    endif()
endfunction()
