# =============================================================================
# LLVM Tool Building Utility
# =============================================================================
# Provides a reusable function for building LLVM/Clang-based tools as external
# projects, with consistent caching, compiler detection, and platform handling.
#
# Usage:
#   build_llvm_tool(
#       NAME <tool_name>                    # e.g., "defer", "panic"
#       SOURCE_DIR <path>                   # Path to tool's CMakeLists.txt directory
#       CACHE_DIR_NAME <name>               # Cache subdirectory name (e.g., "defer-tool")
#       OUTPUT_EXECUTABLE <name>            # Executable name (e.g., "ascii-instr-defer")
#       [PREBUILT_VAR <var_name>]           # Cache variable for pre-built path (e.g., ASCIICHAT_DEFER_TOOL)
#       [PASS_LLVM_CONFIG]                  # Pass LLVM_CONFIG_EXECUTABLE to external project
#       [CLEAN_INCOMPLETE_CACHE]            # Clean partial cache state before building
#       [CREATE_IMPORTED_TARGET <name>]     # Create IMPORTED executable target
#       [ENABLE_LOG_OUTPUT]                 # Enable LOG_CONFIGURE and LOG_BUILD
#       [ISOLATE_FROM_ENV]                  # Clear inherited CXX_FLAGS/EXE_LINKER_FLAGS (non-Windows)
#   )
#
# Returns (in parent scope):
#   ${NAME}_TOOL_EXECUTABLE   - Path to the tool executable
#   ${NAME}_TOOL_DEPENDS      - Target to depend on (empty if using cached/prebuilt)
#   ${NAME}_TOOL_FOUND        - TRUE if tool is available
#
# Example:
#   build_llvm_tool(
#       NAME defer
#       SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/tooling/defer"
#       CACHE_DIR_NAME "defer-tool"
#       OUTPUT_EXECUTABLE "ascii-instr-defer"
#       PREBUILT_VAR ASCIICHAT_DEFER_TOOL
#       PASS_LLVM_CONFIG
#       ENABLE_LOG_OUTPUT
#       ISOLATE_FROM_ENV
#   )
# =============================================================================

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_BUILD_LLVM_TOOL_INCLUDED)
    return()
endif()
set(_ASCIICHAT_BUILD_LLVM_TOOL_INCLUDED TRUE)

include(ExternalProject)

function(build_llvm_tool)
    # Parse arguments
    set(_options PASS_LLVM_CONFIG CLEAN_INCOMPLETE_CACHE ENABLE_LOG_OUTPUT ISOLATE_FROM_ENV)
    set(_one_value_args NAME SOURCE_DIR CACHE_DIR_NAME OUTPUT_EXECUTABLE PREBUILT_VAR CREATE_IMPORTED_TARGET)
    set(_multi_value_args)
    cmake_parse_arguments(_TOOL "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    # Validate required arguments
    if(NOT _TOOL_NAME)
        message(FATAL_ERROR "build_llvm_tool: NAME argument is required")
    endif()
    if(NOT _TOOL_SOURCE_DIR)
        message(FATAL_ERROR "build_llvm_tool: SOURCE_DIR argument is required")
    endif()
    if(NOT _TOOL_CACHE_DIR_NAME)
        message(FATAL_ERROR "build_llvm_tool: CACHE_DIR_NAME argument is required")
    endif()
    if(NOT _TOOL_OUTPUT_EXECUTABLE)
        message(FATAL_ERROR "build_llvm_tool: OUTPUT_EXECUTABLE argument is required")
    endif()

    # Uppercase name for variables
    string(TOUPPER "${_TOOL_NAME}" _TOOL_NAME_UPPER)

    # Initialize output variables
    set(${_TOOL_NAME_UPPER}_TOOL_EXECUTABLE "" PARENT_SCOPE)
    set(${_TOOL_NAME_UPPER}_TOOL_DEPENDS "" PARENT_SCOPE)
    set(${_TOOL_NAME_UPPER}_TOOL_FOUND FALSE PARENT_SCOPE)

    # ==========================================================================
    # Cache Directory Setup
    # ==========================================================================
    set(_cache_dir "${CMAKE_SOURCE_DIR}/.deps-cache/${_TOOL_CACHE_DIR_NAME}")
    set(_cached_exe "${_cache_dir}/${_TOOL_OUTPUT_EXECUTABLE}${CMAKE_EXECUTABLE_SUFFIX}")
    set(_tool_exe "")
    set(_tool_depends "")

    # ==========================================================================
    # Clean Incomplete Cache State (optional)
    # ==========================================================================
    if(_TOOL_CLEAN_INCOMPLETE_CACHE)
        set(_stamp_dir "${CMAKE_BINARY_DIR}/${_TOOL_OUTPUT_EXECUTABLE}-external-prefix/src/${_TOOL_OUTPUT_EXECUTABLE}-external-stamp")
        set(_needs_rebuild FALSE)

        if(EXISTS "${_cache_dir}")
            if(NOT EXISTS "${_cache_dir}/CMakeCache.txt")
                message(STATUS "Cleaning incomplete ${_TOOL_NAME} tool cache (no CMakeCache.txt): ${_cache_dir}")
                file(REMOVE_RECURSE "${_cache_dir}")
                set(_needs_rebuild TRUE)
            elseif(NOT EXISTS "${_cached_exe}")
                message(STATUS "Cleaning incomplete ${_TOOL_NAME} tool cache (no exe): ${_cache_dir}")
                file(REMOVE_RECURSE "${_cache_dir}")
                set(_needs_rebuild TRUE)
            endif()
        else()
            set(_needs_rebuild TRUE)
        endif()

        # Clean stale stamp files if we need a rebuild
        if(_needs_rebuild AND EXISTS "${_stamp_dir}")
            message(STATUS "Cleaning stale ${_TOOL_NAME} tool stamp files: ${_stamp_dir}")
            file(REMOVE_RECURSE "${_stamp_dir}")
        endif()
    endif()

    # ==========================================================================
    # Priority 1: Check for Pre-built Tool
    # ==========================================================================
    if(_TOOL_PREBUILT_VAR AND ${_TOOL_PREBUILT_VAR} AND EXISTS "${${_TOOL_PREBUILT_VAR}}")
        set(_tool_exe "${${_TOOL_PREBUILT_VAR}}")
        message(STATUS "Using external ${_TOOL_NAME} tool: ${_tool_exe}")
    # ==========================================================================
    # Priority 2: Check for Cached Build
    # ==========================================================================
    elseif(EXISTS "${_cached_exe}" AND EXISTS "${_cache_dir}/CMakeCache.txt")
        set(_tool_exe "${_cached_exe}")
        message(STATUS "Using cached ${_TOOL_NAME} tool: ${_tool_exe}")
    # ==========================================================================
    # Priority 3: Build from Source
    # ==========================================================================
    else()
        set(_build_dir "${_cache_dir}")
        set(_tool_exe "${_build_dir}/${_TOOL_OUTPUT_EXECUTABLE}${CMAKE_EXECUTABLE_SUFFIX}")

        # ------------------------------------------------------------------
        # Detect C++ Compiler
        # ------------------------------------------------------------------
        # Prioritize Homebrew LLVM's clang++ to ensure consistency between
        # compiler and linked libraries, and to enable LTO throughout the build.
        if(ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE)
            set(_cxx_compiler "${ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE}")
            message(STATUS "${_TOOL_NAME} tool: Using clang++ from Homebrew LLVM installation: ${_cxx_compiler}")
        elseif(CMAKE_CXX_COMPILER)
            set(_cxx_compiler "${CMAKE_CXX_COMPILER}")
            message(STATUS "${_TOOL_NAME} tool: Using CMAKE_CXX_COMPILER: ${_cxx_compiler}")
        else()
            message(FATAL_ERROR "Cannot find clang++ for building ${_TOOL_NAME} tool. Set CMAKE_CXX_COMPILER or ensure clang++ is in PATH.")
        endif()

        # ------------------------------------------------------------------
        # CMake Arguments for External Project
        # ------------------------------------------------------------------
        set(_cmake_args
            -DCMAKE_CXX_COMPILER=${_cxx_compiler}
            -DCMAKE_BUILD_TYPE=Release
            -DOUTPUT_DIR=${_build_dir}
        )

        # Optionally pass llvm-config path
        if(_TOOL_PASS_LLVM_CONFIG AND ASCIICHAT_LLVM_CONFIG_EXECUTABLE)
            list(APPEND _cmake_args -DLLVM_CONFIG_EXECUTABLE=${ASCIICHAT_LLVM_CONFIG_EXECUTABLE})
            message(STATUS "${_TOOL_NAME} tool: Configuring with llvm-config: ${ASCIICHAT_LLVM_CONFIG_EXECUTABLE}")
        else()
            message(STATUS "${_TOOL_NAME} tool: Configuring without explicit llvm-config")
        endif()

        # ------------------------------------------------------------------
        # Windows vcpkg Integration
        # ------------------------------------------------------------------
        if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
            list(APPEND _cmake_args
                -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
                -DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}
            )
            # Force empty linker flags to prevent -nostartfiles -nostdlib
            set(_cache_args
                -DCMAKE_CXX_COMPILER_WORKS:BOOL=TRUE
                -DCMAKE_C_COMPILER_WORKS:BOOL=TRUE
                -DCMAKE_EXE_LINKER_FLAGS:STRING=
                -DCMAKE_EXE_LINKER_FLAGS_DEBUG:STRING=
                -DCMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=
                -DCMAKE_SHARED_LINKER_FLAGS:STRING=
                -DCMAKE_MODULE_LINKER_FLAGS:STRING=
                -DCMAKE_PLATFORM_REQUIRED_RUNTIME_PATH:STRING=
            )
        elseif(_TOOL_ISOLATE_FROM_ENV)
            # Isolate from inherited environment flags (e.g., makepkg's -flto=auto)
            set(_cache_args
                -DCMAKE_CXX_FLAGS:STRING=
                -DCMAKE_EXE_LINKER_FLAGS:STRING=
            )
        else()
            set(_cache_args "")
        endif()

        # ------------------------------------------------------------------
        # Verify Source Directory
        # ------------------------------------------------------------------
        set(_cmake_file "${_TOOL_SOURCE_DIR}/CMakeLists.txt")
        if(NOT EXISTS "${_cmake_file}")
            message(FATAL_ERROR
                "ERROR: ${_TOOL_NAME} tool CMakeLists.txt not found at: ${_cmake_file}\n"
                "The ${_TOOL_NAME} tool requires a CMakeLists.txt file in ${_TOOL_SOURCE_DIR}/ to build.\n"
                "Please ensure ${_TOOL_SOURCE_DIR}/CMakeLists.txt exists in the repository."
            )
        endif()

        # ------------------------------------------------------------------
        # Configure ExternalProject
        # ------------------------------------------------------------------
        set(_external_target "${_TOOL_OUTPUT_EXECUTABLE}-external")

        # Build ExternalProject_Add arguments
        set(_ep_args
            SOURCE_DIR "${_TOOL_SOURCE_DIR}"
            BINARY_DIR "${_build_dir}"
            CMAKE_ARGS ${_cmake_args}
            BUILD_ALWAYS FALSE
            INSTALL_COMMAND ${CMAKE_COMMAND} -E true
            BUILD_BYPRODUCTS "${_tool_exe}"
        )

        if(_cache_args)
            list(APPEND _ep_args CMAKE_CACHE_ARGS ${_cache_args})
        endif()

        # Always show configure and build output for debugging
        list(APPEND _ep_args LOG_CONFIGURE FALSE LOG_BUILD FALSE)

        ExternalProject_Add(${_external_target} ${_ep_args})

        set(_tool_depends "${_external_target}")
        message(STATUS "Building ${_TOOL_NAME} tool to cache: ${_cache_dir}")
        message(STATUS "  (To force rebuild, delete: ${_cached_exe})")
    endif()

    # ==========================================================================
    # Create Imported Target (optional)
    # ==========================================================================
    if(_TOOL_CREATE_IMPORTED_TARGET)
        if(NOT TARGET ${_TOOL_CREATE_IMPORTED_TARGET})
            add_executable(${_TOOL_CREATE_IMPORTED_TARGET} IMPORTED GLOBAL)
            set_target_properties(${_TOOL_CREATE_IMPORTED_TARGET} PROPERTIES
                IMPORTED_LOCATION "${_tool_exe}"
            )
            if(_tool_depends)
                add_dependencies(${_TOOL_CREATE_IMPORTED_TARGET} ${_tool_depends})
            endif()
        endif()
    endif()

    # ==========================================================================
    # Set Output Variables
    # ==========================================================================
    set(${_TOOL_NAME_UPPER}_TOOL_EXECUTABLE "${_tool_exe}" PARENT_SCOPE)
    set(${_TOOL_NAME_UPPER}_TOOL_DEPENDS "${_tool_depends}" PARENT_SCOPE)
    set(${_TOOL_NAME_UPPER}_TOOL_FOUND TRUE PARENT_SCOPE)
endfunction()

