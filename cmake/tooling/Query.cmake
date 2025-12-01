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

include(ExternalProject)

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

    # Determine which query tool to use
    # Priority: 1. ASCIICHAT_QUERY_TOOL (explicit), 2. Cached build, 3. Build new
    set(_query_cache_dir "${CMAKE_SOURCE_DIR}/.deps-cache/query-tool")
    set(_query_cached_exe "${_query_cache_dir}/ascii-query-server${CMAKE_EXECUTABLE_SUFFIX}")

    # Clean up any partial cache state BEFORE checking if exe exists
    set(_query_stamp_dir "${CMAKE_BINARY_DIR}/ascii-query-server-external-prefix/src/ascii-query-server-external-stamp")
    set(_query_needs_rebuild FALSE)

    if(EXISTS "${_query_cache_dir}")
        if(NOT EXISTS "${_query_cache_dir}/CMakeCache.txt")
            message(STATUS "Cleaning incomplete query tool cache (no CMakeCache.txt): ${_query_cache_dir}")
            file(REMOVE_RECURSE "${_query_cache_dir}")
            set(_query_needs_rebuild TRUE)
        elseif(NOT EXISTS "${_query_cached_exe}")
            message(STATUS "Cleaning incomplete query tool cache (no exe): ${_query_cache_dir}")
            file(REMOVE_RECURSE "${_query_cache_dir}")
            set(_query_needs_rebuild TRUE)
        endif()
    else()
        set(_query_needs_rebuild TRUE)
    endif()

    # Clean up stale stamp files if we need a rebuild
    if(_query_needs_rebuild AND EXISTS "${_query_stamp_dir}")
        message(STATUS "Cleaning stale query tool stamp files: ${_query_stamp_dir}")
        file(REMOVE_RECURSE "${_query_stamp_dir}")
    endif()

    if(ASCIICHAT_QUERY_TOOL AND EXISTS "${ASCIICHAT_QUERY_TOOL}")
        set(_query_tool_exe "${ASCIICHAT_QUERY_TOOL}")
        set(_query_tool_depends "")
        message(STATUS "Using external query tool: ${_query_tool_exe}")
    elseif(EXISTS "${_query_cached_exe}" AND EXISTS "${_query_cache_dir}/CMakeCache.txt")
        # Use cached query tool (persists across build directory deletes)
        set(_query_tool_exe "${_query_cached_exe}")
        set(_query_tool_depends "")
        message(STATUS "Using cached query tool: ${_query_tool_exe}")
    else()
        # Build query tool to cache directory (survives rm -rf build)
        set(_query_build_dir "${_query_cache_dir}")
        set(_query_tool_exe "${_query_build_dir}/ascii-query-server${CMAKE_EXECUTABLE_SUFFIX}")

        # Detect the C++ compiler for building the query tool
        # On macOS, use Apple's system clang for compilation (Homebrew LLVM for libraries)
        if(APPLE AND EXISTS "/usr/bin/clang++")
            set(_query_cxx_compiler "/usr/bin/clang++")
            message(STATUS "Query tool: Using Apple system clang for compilation: ${_query_cxx_compiler}")
        elseif(CMAKE_CXX_COMPILER)
            set(_query_cxx_compiler "${CMAKE_CXX_COMPILER}")
        elseif(ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE)
            set(_query_cxx_compiler "${ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE}")
        else()
            message(FATAL_ERROR "Cannot find clang++ for building query tool. Set CMAKE_CXX_COMPILER or ensure clang++ is in PATH.")
        endif()

        # CMake arguments for the external project
        set(_query_cmake_args
            -DCMAKE_CXX_COMPILER=${_query_cxx_compiler}
            -DCMAKE_BUILD_TYPE=Release
            -DOUTPUT_DIR=${_query_build_dir}
        )

        # On Windows with vcpkg, pass the vcpkg root
        if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
            list(APPEND _query_cmake_args
                -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
                -DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}
            )
            set(_query_cache_args
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
            set(_query_cache_args "")
        endif()

        # Verify that the query tool CMakeLists.txt exists
        set(_query_source_dir "${CMAKE_SOURCE_DIR}/src/tooling/query")
        set(_query_cmake_file "${_query_source_dir}/CMakeLists.txt")
        if(NOT EXISTS "${_query_cmake_file}")
            message(FATAL_ERROR
                "ERROR: Query tool CMakeLists.txt not found at: ${_query_cmake_file}\n"
                "The query tool requires a CMakeLists.txt file in src/tooling/query/ to build.\n"
                "Please ensure src/tooling/query/CMakeLists.txt exists in the repository."
            )
        endif()

        ExternalProject_Add(ascii-query-server-external
            SOURCE_DIR "${_query_source_dir}"
            BINARY_DIR "${_query_build_dir}"
            CMAKE_ARGS ${_query_cmake_args}
            CMAKE_CACHE_ARGS ${_query_cache_args}
            BUILD_ALWAYS FALSE
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${_query_tool_exe}"
        )

        set(_query_tool_depends ascii-query-server-external)
        message(STATUS "Building query tool to cache: ${_query_cache_dir}")
        message(STATUS "  (To force rebuild, delete: ${_query_cached_exe})")
    endif()

    # Store the tool path for later use
    set(ASCII_QUERY_TOOL_EXE "${_query_tool_exe}" PARENT_SCOPE)
    set(ASCII_QUERY_TOOL_DEPENDS "${_query_tool_depends}" PARENT_SCOPE)
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
    endif()
endfunction()
