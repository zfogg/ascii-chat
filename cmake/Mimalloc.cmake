# =============================================================================
# mimalloc Memory Allocator Configuration
# =============================================================================
# Configures and builds Microsoft's mimalloc high-performance allocator
#
# Prerequisites (must be set before including this file):
#   - USE_MIMALLOC: Option to enable mimalloc
#   - USE_MUSL: Whether using musl libc (affects override behavior)
#   - FETCHCONTENT_BASE_DIR: Where to cache the mimalloc build
#   - CMAKE_BUILD_TYPE: Build type (affects debug settings)
#   - REAL_GCC: (Optional) For musl builds
#
# Outputs:
#   - MIMALLOC_LIBRARIES: Target to link against (mimalloc-static)
#   - Defines: USE_MIMALLOC, MI_STATIC_LIB (via add_compile_definitions)
# =============================================================================

if(USE_MIMALLOC)
    message(STATUS "Configuring mimalloc memory allocator...")

    # Check if mimalloc library already exists in cache
    set(_MIMALLOC_LIB_PATH "${FETCHCONTENT_BASE_DIR}/mimalloc-build/lib/mimalloc-static.lib")
    if(NOT WIN32)
        set(_MIMALLOC_LIB_PATH "${FETCHCONTENT_BASE_DIR}/mimalloc-build/lib/libmimalloc-static.a")
    endif()

    if(EXISTS "${_MIMALLOC_LIB_PATH}")
        message(STATUS "Using cached mimalloc library: ${_MIMALLOC_LIB_PATH}")

        # Create an imported target for the cached library
        add_library(mimalloc-static STATIC IMPORTED)
        set_target_properties(mimalloc-static PROPERTIES
            IMPORTED_LOCATION "${_MIMALLOC_LIB_PATH}"
            INTERFACE_INCLUDE_DIRECTORIES "${FETCHCONTENT_BASE_DIR}/mimalloc-src/include"
        )

        # Skip FetchContent download/build
        set(_MIMALLOC_CACHED TRUE)
    else()
        # Download and build mimalloc from GitHub
        message(STATUS "Downloading mimalloc from GitHub...")

        include(FetchContent)

        FetchContent_Declare(
            mimalloc
            GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
            GIT_TAG v2.1.7  # Latest stable v2.x release
            GIT_SHALLOW TRUE
            # Use persistent cache directories (survives build/ deletion)
            SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/mimalloc-src"
            BINARY_DIR "${FETCHCONTENT_BASE_DIR}/mimalloc-build"
        )

        set(_MIMALLOC_CACHED FALSE)
    endif()

    # Only configure and build if not using cached library
    if(NOT _MIMALLOC_CACHED)
        # Configure mimalloc build options
        set(MI_BUILD_SHARED OFF CACHE BOOL "Build shared library")
        set(MI_BUILD_STATIC ON CACHE BOOL "Build static library")
        set(MI_BUILD_OBJECT OFF CACHE BOOL "Build object library")
        set(MI_BUILD_TESTS OFF CACHE BOOL "Build test executables")

        # For musl, disable MI_OVERRIDE to avoid symbol conflicts with musl's malloc
        # We'll explicitly call mi_malloc instead of relying on override
        if(USE_MUSL)
            set(MI_OVERRIDE OFF CACHE BOOL "Don't override malloc for musl - use explicit calls")
        else()
            set(MI_OVERRIDE ON CACHE BOOL "Override malloc/free globally")
        endif()

        # Completely disable mimalloc installation - it's statically linked
        set(MI_INSTALL_TOPLEVEL OFF CACHE BOOL "Install in top-level")

        # Set debug level for mimalloc based on build type
        if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
            set(MI_DEBUG_FULL ON CACHE BOOL "Full debug mode for mimalloc")
        endif()

        # Platform-specific mimalloc options
        if(WIN32)
            set(MI_WIN_REDIRECT ON CACHE BOOL "Redirect malloc on Windows")
        endif()

        # For musl builds, use local dynamic TLS to avoid conflicts
        if(USE_MUSL)
            set(MI_LOCAL_DYNAMIC_TLS ON CACHE BOOL "Use local dynamic TLS for musl compatibility")
            message(STATUS "Enabling MI_LOCAL_DYNAMIC_TLS for musl compatibility")
        endif()

        # Save current output directory settings
        set(_SAVED_ARCHIVE_OUTPUT_DIR ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
        set(_SAVED_LIBRARY_OUTPUT_DIR ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

        # Set output directories to persistent cache (so library survives build/ deletion)
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${FETCHCONTENT_BASE_DIR}/mimalloc-build/lib")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${FETCHCONTENT_BASE_DIR}/mimalloc-build/lib")

        # Populate and build mimalloc
        FetchContent_MakeAvailable(mimalloc)

        # Restore original output directory settings
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_SAVED_ARCHIVE_OUTPUT_DIR})
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_SAVED_LIBRARY_OUTPUT_DIR})

        # Fix C23 compatibility issues with mimalloc
        if(TARGET mimalloc-static)
            # Add errno.h include for C23 compatibility
            target_compile_options(mimalloc-static PRIVATE -include errno.h)
            # Disable warnings for mimalloc (third-party code)
            target_compile_options(mimalloc-static PRIVATE -Wno-undef)
            target_compile_options(mimalloc-static PRIVATE -Wno-strict-prototypes)
            message(STATUS "Added errno.h include and warning suppressions for mimalloc")
        endif()

        # For musl builds, set REALGCC environment for mimalloc build
        if(USE_MUSL AND REAL_GCC AND TARGET mimalloc-static)
            set_target_properties(mimalloc-static PROPERTIES
                COMPILE_OPTIONS ""
                RULE_LAUNCH_COMPILE "env REALGCC=${REAL_GCC}"
            )
            message(STATUS "Configured mimalloc build environment with REALGCC=${REAL_GCC}")
        endif()

        message(STATUS "Built mimalloc from source (v2.1.7) with installation disabled")
    endif()

    set(MIMALLOC_LIBRARIES mimalloc-static)

    # Define USE_MIMALLOC for all source files so they can use mi_malloc/mi_free directly
    # Also define MI_STATIC_LIB to tell mimalloc headers we're using static library (prevents dllimport)
    add_compile_definitions(USE_MIMALLOC MI_STATIC_LIB)

    message(STATUS "mimalloc will override malloc/free globally")
    message(STATUS "Your existing SAFE_MALLOC macros will automatically use mimalloc")
endif()
