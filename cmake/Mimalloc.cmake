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
        # Configure mimalloc build options - disable all built-in targets
        # We'll create our own mimalloc-static and mimalloc-shared targets
        set(MI_BUILD_SHARED OFF CACHE BOOL "Build shared library")
        set(MI_BUILD_STATIC OFF CACHE BOOL "Build static library")
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

        # Make mimalloc sources available (modern CMake approach)
        FetchContent_MakeAvailable(mimalloc)

        # Restore original output directory settings
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_SAVED_ARCHIVE_OUTPUT_DIR})
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_SAVED_LIBRARY_OUTPUT_DIR})

        # Manually create mimalloc targets with different compile flags
        # Use static.c which includes all other source files (single compilation unit)
        set(MIMALLOC_SRCS "${FETCHCONTENT_BASE_DIR}/mimalloc-src/src/static.c")

        # Common compile options for both targets
        set(MIMALLOC_COMMON_OPTIONS
            -include errno.h  # C23 compatibility
            -Wno-undef       # Suppress mimalloc warnings
            -Wno-strict-prototypes
        )

        # Target 1: mimalloc-static (for executables, uses global -fPIE)
        add_library(mimalloc-static STATIC ${MIMALLOC_SRCS})
        target_include_directories(mimalloc-static PUBLIC
            "${FETCHCONTENT_BASE_DIR}/mimalloc-src/include"
        )
        target_compile_options(mimalloc-static PRIVATE ${MIMALLOC_COMMON_OPTIONS})
        set_target_properties(mimalloc-static PROPERTIES
            OUTPUT_NAME "mimalloc"
            ARCHIVE_OUTPUT_DIRECTORY "${FETCHCONTENT_BASE_DIR}/mimalloc-build/lib"
        )

        # Target 2: mimalloc-shared (for shared library, uses -fPIC and global-dynamic TLS)
        add_library(mimalloc-shared STATIC ${MIMALLOC_SRCS})
        target_include_directories(mimalloc-shared PUBLIC
            "${FETCHCONTENT_BASE_DIR}/mimalloc-src/include"
        )
        target_compile_options(mimalloc-shared PRIVATE
            ${MIMALLOC_COMMON_OPTIONS}
            -fno-pie                    # Disable PIE
            -fPIC                       # Enable PIC for shared library
            -ftls-model=global-dynamic  # Correct TLS model for shared libraries
        )
        set_target_properties(mimalloc-shared PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            OUTPUT_NAME "mimalloc-shared"
            ARCHIVE_OUTPUT_DIRECTORY "${FETCHCONTENT_BASE_DIR}/mimalloc-build/lib"
        )

        # For musl builds, set REALGCC environment for both targets
        if(USE_MUSL AND REAL_GCC)
            set_target_properties(mimalloc-static PROPERTIES
                RULE_LAUNCH_COMPILE "env REALGCC=${REAL_GCC}"
            )
            set_target_properties(mimalloc-shared PROPERTIES
                RULE_LAUNCH_COMPILE "env REALGCC=${REAL_GCC}"
            )
            message(STATUS "Configured mimalloc build environment with REALGCC=${REAL_GCC}")
        endif()

        message(STATUS "Created two mimalloc targets:")
        message(STATUS "  - mimalloc-static: for executables (uses -fPIE from global flags)")
        message(STATUS "  - mimalloc-shared: for shared libraries (uses -fPIC and global-dynamic TLS)")

        message(STATUS "Built mimalloc from source (v2.1.7) with two targets: mimalloc-static and mimalloc-shared")
    endif()

    set(MIMALLOC_LIBRARIES mimalloc-static)
    set(MIMALLOC_SHARED_LIBRARIES mimalloc-shared)

    # Define USE_MIMALLOC for all source files so they can use mi_malloc/mi_free directly
    # Also define MI_STATIC_LIB to tell mimalloc headers we're using static library (prevents dllimport)
    add_compile_definitions(USE_MIMALLOC MI_STATIC_LIB)

    message(STATUS "mimalloc will override malloc/free globally")
    message(STATUS "Your existing SAFE_MALLOC macros will automatically use mimalloc")
endif()
