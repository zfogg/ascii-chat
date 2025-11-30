# =============================================================================
# Tooling Runtime Library Module
# =============================================================================
# This module builds the panic instrumentation runtime library and optional
# analysis utilities. The runtime is linked into ascii-chat when panic
# instrumentation is enabled.
#
# Components:
#   - ascii-panic-runtime: Runtime library for panic instrumentation logging
#   - ascii-panic-report: Optional log analysis utility (Unix only)
#
# Note: The panic and defer TOOLS (ascii-instr-panic, ascii-instr-defer) are
# built separately via ExternalProject in Panic.cmake and Defer.cmake to
# avoid inheriting musl/static flags from the main build.
# =============================================================================

include_guard(GLOBAL)

function(ascii_build_tooling_runtime)
    # Skip if already built
    if(TARGET ascii-panic-runtime)
        return()
    endif()

    # =========================================================================
    # Panic Instrumentation Runtime Library
    # =========================================================================
    # This library provides the runtime logging functions called by
    # panic-instrumented code. It must be built as part of the main project
    # because it depends on ascii-chat libraries (common.h, platform, etc.)

    if(TARGET ascii-chat-panic)
        # Use the existing panic library module
        add_library(ascii-panic-runtime INTERFACE)
        target_link_libraries(ascii-panic-runtime INTERFACE ascii-chat-panic)
    else()
        # Build runtime from source files
        add_library(ascii-panic-runtime STATIC ${TOOLING_PANIC_SRCS})
        target_include_directories(ascii-panic-runtime
            PUBLIC
                ${CMAKE_SOURCE_DIR}/lib
                ${CMAKE_BINARY_DIR}/generated
        )

        # Add mimalloc include directory if enabled
        if(USE_MIMALLOC)
            set(_runtime_mimalloc_includes "")
            if(TARGET mimalloc-static)
                get_target_property(_mimalloc_iface mimalloc-static INTERFACE_INCLUDE_DIRECTORIES)
                if(_mimalloc_iface)
                    list(APPEND _runtime_mimalloc_includes ${_mimalloc_iface})
                endif()
            endif()
            if(DEFINED MIMALLOC_INCLUDE_DIR AND MIMALLOC_INCLUDE_DIR)
                list(APPEND _runtime_mimalloc_includes "${MIMALLOC_INCLUDE_DIR}")
            endif()
            if(DEFINED MIMALLOC_SOURCE_DIR AND EXISTS "${MIMALLOC_SOURCE_DIR}/include")
                list(APPEND _runtime_mimalloc_includes "${MIMALLOC_SOURCE_DIR}/include")
            elseif(DEFINED FETCHCONTENT_BASE_DIR AND EXISTS "${FETCHCONTENT_BASE_DIR}/mimalloc-src/include")
                list(APPEND _runtime_mimalloc_includes "${FETCHCONTENT_BASE_DIR}/mimalloc-src/include")
            endif()
            list(REMOVE_DUPLICATES _runtime_mimalloc_includes)
            if(_runtime_mimalloc_includes)
                target_include_directories(ascii-panic-runtime PRIVATE ${_runtime_mimalloc_includes})
            endif()
        endif()

        # Link pthread on Unix (Windows uses platform abstraction)
        if(NOT WIN32 AND TARGET Threads::Threads)
            target_link_libraries(ascii-panic-runtime PUBLIC Threads::Threads)
        endif()

        set_target_properties(ascii-panic-runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()

    # =========================================================================
    # Panic Report Utility (Optional)
    # =========================================================================
    # This utility reads panic instrumentation log files and summarizes them.
    # Cross-platform: uses dirent.h on Unix, _findfirst/_findnext on Windows.

    add_executable(ascii-panic-report
        ${TOOLING_PANIC_REPORT_SRCS}
    )
    target_include_directories(ascii-panic-report
        PRIVATE
            ${CMAKE_SOURCE_DIR}/lib
            ${CMAKE_BINARY_DIR}/generated
    )

    # Add mimalloc include if enabled (needed for common.h)
    if(USE_MIMALLOC AND DEFINED FETCHCONTENT_BASE_DIR)
        target_include_directories(ascii-panic-report
            PRIVATE
                "${FETCHCONTENT_BASE_DIR}/mimalloc-src/include"
        )
    endif()

    # On Windows Debug/Dev builds, use shared library; otherwise use static
    if(WIN32 AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev" OR CMAKE_BUILD_TYPE STREQUAL "Coverage"))
        target_link_libraries(ascii-panic-report ascii-chat-shared)
    else()
        target_link_libraries(ascii-panic-report ascii-chat-static)
    endif()
    set_target_properties(ascii-panic-report PROPERTIES OUTPUT_NAME "ascii-panic-report")

endfunction()

# =============================================================================
# Windows DLL Copying for Tooling
# =============================================================================
# The tooling executables (ascii-instr-panic, ascii-instr-defer) need
# zstd.dll and zlibd1.dll at runtime. This is handled by each tool's
# ExternalProject post-build step, but we provide a fallback here.

function(ascii_copy_tooling_dlls)
    if(NOT WIN32 OR NOT DEFINED ENV{VCPKG_ROOT})
        return()
    endif()

    # DLL source directory
    set(_dll_source "$ENV{VCPKG_ROOT}/installed/x64-windows/debug/bin")

    # Copy DLLs for ascii-instr-defer (if built locally, not cached)
    if(TARGET ascii-instr-defer AND NOT ASCII_DEFER_TOOL_CACHED)
        add_custom_command(TARGET ascii-instr-defer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll_source}/zstd.dll"
                "$<TARGET_FILE_DIR:ascii-instr-defer>/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll_source}/zlibd1.dll"
                "$<TARGET_FILE_DIR:ascii-instr-defer>/"
            COMMENT "Copying zstd.dll and zlibd1.dll for ascii-instr-defer"
            VERBATIM
        )
    endif()

    # Copy DLLs for ascii-instr-panic (if built locally, not cached)
    if(TARGET ascii-instr-panic AND NOT ASCII_PANIC_TOOL_CACHED)
        add_custom_command(TARGET ascii-instr-panic POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll_source}/zstd.dll"
                "$<TARGET_FILE_DIR:ascii-instr-panic>/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll_source}/zlibd1.dll"
                "$<TARGET_FILE_DIR:ascii-instr-panic>/"
            COMMENT "Copying zstd.dll and zlibd1.dll for ascii-instr-panic"
            VERBATIM
        )
    endif()
endfunction()
