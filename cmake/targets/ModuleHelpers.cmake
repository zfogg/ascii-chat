# =============================================================================
# Module Helpers
# =============================================================================
# Reusable macros and functions for creating ascii-chat library modules.
# This file is included by lib/CMakeLists.txt and provides:
#   - create_ascii_chat_module() macro for creating library modules
#   - Common configuration shared across all modules
#
# Prerequisites:
#   - Platform detection complete (WIN32, PLATFORM_DARWIN, etc.)
#   - USE_MIMALLOC, USE_MUSL known
#   - CMAKE_BUILD_TYPE set
# =============================================================================

include(CheckCCompilerFlag)

# Check for warning flags that may not exist in older Clang versions
# -Wno-unterminated-string-initialization was added in Clang 20
check_c_compiler_flag("-Wno-unterminated-string-initialization" HAVE_WNO_UNTERMINATED_STRING_INIT)

# Check if we're building OBJECT libraries (Windows dev builds)
if(WIN32 AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev"))
    set(BUILDING_OBJECT_LIBS TRUE CACHE INTERNAL "Building OBJECT libraries for Windows DLL")
else()
    set(BUILDING_OBJECT_LIBS FALSE CACHE INTERNAL "Building STATIC libraries")
endif()

# =============================================================================
# create_ascii_chat_module() - Create a library module with standard settings
# =============================================================================
# Arguments:
#   MODULE_NAME - Name of the module target (e.g., ascii-chat-util)
#   MODULE_SRCS - List of source files for this module
#
# This macro:
#   - Creates OBJECT or STATIC library based on build configuration
#   - Sets up include directories for generated headers
#   - Configures mimalloc and musl flags if enabled
#   - Adds version generation dependency
# =============================================================================
macro(create_ascii_chat_module MODULE_NAME MODULE_SRCS)
    # For Windows Debug/Dev builds: use OBJECT libraries so we can build a proper DLL
    # For other platforms/builds: use STATIC libraries
    if(BUILDING_OBJECT_LIBS)
        add_library(${MODULE_NAME} OBJECT ${MODULE_SRCS})
        # Mark all symbols for export when building DLL from OBJECT libraries
        target_compile_definitions(${MODULE_NAME} PRIVATE
            _WIN32_WINNT=0x0A00  # Windows 10
            BUILDING_ASCIICHAT_DLL=1
        )
    else()
        # Module libraries are intermediate build artifacts
        # They should not be in the 'all' target by default
        add_library(${MODULE_NAME} STATIC EXCLUDE_FROM_ALL ${MODULE_SRCS})
        # For static library builds on Windows, define BUILDING_STATIC_LIB
        # so that ASCIICHAT_API expands to nothing (not dllimport)
        if(WIN32)
            target_compile_definitions(${MODULE_NAME} PRIVATE
                _WIN32_WINNT=0x0A00  # Windows 10
                BUILDING_STATIC_LIB=1
            )
        endif()
        # Enable Position Independent Code for shared library builds
        # Required for thread-local storage (TLS) relocations in shared objects
        set_target_properties(${MODULE_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()

    if(ASCIICHAT_ENABLE_UNITY_BUILDS)
        set_target_properties(${MODULE_NAME} PROPERTIES UNITY_BUILD ON)
    endif()

    if(ASCIICHAT_ENABLE_IPO)
        set_property(TARGET ${MODULE_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

    # Version dependency (build-timer-start removed to prevent unnecessary rebuilds)
    add_dependencies(${MODULE_NAME} generate_version)

    # Include paths
    target_include_directories(${MODULE_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include/ascii-chat>
    )

    # Build directory for llvm-symbolizer --debug-file-directory (debug builds only)
    # Only include BUILD_DIR in debug builds to avoid embedding build paths in release binaries
    # Note: Release builds will not have BUILD_DIR defined, so llvm-symbolizer will work without it
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
        target_compile_definitions(${MODULE_NAME} PRIVATE BUILD_DIR="${CMAKE_BINARY_DIR}")
    endif()

    # MI_DEBUG for mimalloc
    if(DEFINED MIMALLOC_DEBUG_LEVEL)
        target_compile_definitions(${MODULE_NAME} PRIVATE MI_DEBUG=${MIMALLOC_DEBUG_LEVEL})
    endif()

    # Mimalloc include directory (use MIMALLOC_INCLUDE_DIRS for system vs FetchContent)
    if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
        target_include_directories(${MODULE_NAME} PRIVATE ${MIMALLOC_INCLUDE_DIRS})
    endif()

    # Musl flag
    if(USE_MUSL)
        target_compile_definitions(${MODULE_NAME} PRIVATE USE_MUSL=1)
    endif()
endmacro()
