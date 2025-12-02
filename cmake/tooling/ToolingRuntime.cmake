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
        # Use MIMALLOC_INCLUDE_DIRS which is already set in Mimalloc.cmake
        # and handles system, vcpkg, and FetchContent mimalloc installations
        if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
            target_include_directories(ascii-panic-runtime PRIVATE ${MIMALLOC_INCLUDE_DIRS})
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
    # Use MIMALLOC_INCLUDE_DIRS which handles system, vcpkg, and FetchContent mimalloc
    if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
        target_include_directories(ascii-panic-report PRIVATE ${MIMALLOC_INCLUDE_DIRS})
    endif()

    # Debug/Dev builds use shared library; Release/RelWithDebInfo uses static
    # USE_MUSL always needs static because musl requires static linking
    if((CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev") AND NOT USE_MUSL)
        target_link_libraries(ascii-panic-report ascii-chat-shared)
        # Link mimalloc explicitly - shared library links it PRIVATE so symbols don't propagate
        if(USE_MIMALLOC AND MIMALLOC_LIBRARIES)
            target_link_libraries(ascii-panic-report ${MIMALLOC_LIBRARIES})
        endif()
    else()
        target_link_libraries(ascii-panic-report ascii-chat-static)
        # ascii-chat-static is an INTERFACE library - add explicit build dependency
        # ASCII_CHAT_UNIFIED_BUILD_TARGET is set in Libraries.cmake to the actual build target
        if(TARGET ascii-chat-static-build)
            add_dependencies(ascii-panic-report ascii-chat-static-build)
        endif()
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
