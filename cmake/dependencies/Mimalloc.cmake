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
#   - VCPKG_ROOT: (Windows only) vcpkg installation path
#   - VCPKG_TRIPLET: (Windows only) vcpkg triplet (e.g., x64-windows-static)
#
# Outputs:
#   - MIMALLOC_LIBRARIES: Target to link against (mimalloc-static)
#   - Defines: USE_MIMALLOC, MI_STATIC_LIB (via add_compile_definitions)
# =============================================================================

if(USE_MIMALLOC)
    message(STATUS "Configuring ${BoldCyan}mimalloc${ColorReset} memory allocator...")

    # On Windows, prefer vcpkg; on Unix, use FetchContent
    if(WIN32 AND DEFINED VCPKG_TRIPLET AND DEFINED VCPKG_ROOT)
        # Try to find mimalloc from vcpkg
        set(VCPKG_INCLUDE_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/include")
        set(VCPKG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/lib")
        set(VCPKG_DEBUG_LIB_PATH "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/debug/lib")

        find_library(MIMALLOC_LIBRARY_RELEASE NAMES mimalloc-static mimalloc PATHS "${VCPKG_LIB_PATH}" NO_DEFAULT_PATH)
        find_library(MIMALLOC_LIBRARY_DEBUG NAMES mimalloc-static mimalloc PATHS "${VCPKG_DEBUG_LIB_PATH}" NO_DEFAULT_PATH)
        find_path(MIMALLOC_INCLUDE_DIR NAMES mimalloc.h PATHS "${VCPKG_INCLUDE_PATH}" NO_DEFAULT_PATH)

        if(MIMALLOC_LIBRARY_RELEASE OR MIMALLOC_LIBRARY_DEBUG)
            # Try to get version from vcpkg (version is in directory path)
            string(REGEX MATCH "mimalloc_([0-9]+\\.[0-9]+\\.[0-9]+)" MIMALLOC_VERSION_MATCH "${MIMALLOC_LIBRARY_RELEASE}")
            if(CMAKE_MATCH_1)
                set(MIMALLOC_VERSION "${CMAKE_MATCH_1}")
                message(STATUS "Found ${BoldGreen}mimalloc${ColorReset} from vcpkg, version ${BoldGreen}${MIMALLOC_VERSION}${ColorReset}: ${BoldCyan}${MIMALLOC_LIBRARY_RELEASE}${ColorReset}")
            else()
                message(STATUS "Found ${BoldGreen}mimalloc${ColorReset} from vcpkg: ${BoldCyan}${MIMALLOC_LIBRARY_RELEASE}${ColorReset}")
            endif()

            # Create imported target
            add_library(mimalloc-static STATIC IMPORTED)
            if(MIMALLOC_LIBRARY_RELEASE AND MIMALLOC_LIBRARY_DEBUG)
                set_target_properties(mimalloc-static PROPERTIES
                    IMPORTED_LOCATION_RELEASE "${MIMALLOC_LIBRARY_RELEASE}"
                    IMPORTED_LOCATION_DEBUG "${MIMALLOC_LIBRARY_DEBUG}"
                    INTERFACE_INCLUDE_DIRECTORIES "${MIMALLOC_INCLUDE_DIR}"
                )
            elseif(MIMALLOC_LIBRARY_RELEASE)
                set_target_properties(mimalloc-static PROPERTIES
                    IMPORTED_LOCATION "${MIMALLOC_LIBRARY_RELEASE}"
                    INTERFACE_INCLUDE_DIRECTORIES "${MIMALLOC_INCLUDE_DIR}"
                )
            else()
                set_target_properties(mimalloc-static PROPERTIES
                    IMPORTED_LOCATION "${MIMALLOC_LIBRARY_DEBUG}"
                    INTERFACE_INCLUDE_DIRECTORIES "${MIMALLOC_INCLUDE_DIR}"
                )
            endif()

            set(MIMALLOC_LIBRARIES mimalloc-static)
            get_target_property(_mimalloc_vcpkg_static mimalloc-static IMPORTED_LOCATION)
            if(_mimalloc_vcpkg_static)
            set(MIMALLOC_LIBRARIES "${_mimalloc_vcpkg_static}")
            set(ASCIICHAT_MIMALLOC_LINK_LIB "${_mimalloc_vcpkg_static}")
            endif()
            set(_MIMALLOC_FROM_VCPKG TRUE)
        else()
            message(WARNING "Could not find ${BoldYellow}mimalloc${ColorReset} from vcpkg - falling back to FetchContent")
            set(_MIMALLOC_FROM_VCPKG FALSE)
        endif()
    else()
        set(_MIMALLOC_FROM_VCPKG FALSE)
    endif()

    # Fall back to FetchContent if not using vcpkg or vcpkg didn't have mimalloc
    if(NOT _MIMALLOC_FROM_VCPKG)
        set(MIMALLOC_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/mimalloc-src")
        set(MIMALLOC_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/mimalloc")

        # Check if mimalloc library already exists in cache
        # For Debug builds with ASAN, look for ASAN-enabled library first
        if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT WIN32)
            # Unix: Try ASAN library first (libmimalloc-asan-debug.a), fallback to static
            set(_MIMALLOC_LIB_PATH "${MIMALLOC_BUILD_DIR}/lib/libmimalloc-asan-debug.a")
            if(NOT EXISTS "${_MIMALLOC_LIB_PATH}")
                set(_MIMALLOC_LIB_PATH "${MIMALLOC_BUILD_DIR}/lib/libmimalloc-static.a")
            endif()
        elseif(CMAKE_BUILD_TYPE STREQUAL "Debug" AND WIN32)
            # Windows: Try ASAN library first, fallback to static
            set(_MIMALLOC_LIB_PATH "${MIMALLOC_BUILD_DIR}/lib/mimalloc-asan-debug.lib")
            if(NOT EXISTS "${_MIMALLOC_LIB_PATH}")
                set(_MIMALLOC_LIB_PATH "${MIMALLOC_BUILD_DIR}/lib/mimalloc-static.lib")
            endif()
        else()
            # Non-Debug builds: Use regular static library
            set(_MIMALLOC_LIB_PATH "${MIMALLOC_BUILD_DIR}/lib/mimalloc-static.lib")
            if(NOT WIN32)
                set(_MIMALLOC_LIB_PATH "${MIMALLOC_BUILD_DIR}/lib/libmimalloc-static.a")
                if(NOT EXISTS "${_MIMALLOC_LIB_PATH}")
                    set(_MIMALLOC_LIB_PATH "${MIMALLOC_BUILD_DIR}/lib/libmimalloc.a")
                endif()
            elseif(NOT EXISTS "${_MIMALLOC_LIB_PATH}")
                set(_MIMALLOC_LIB_PATH "${MIMALLOC_BUILD_DIR}/lib/mimalloc.lib")
            endif()
        endif()

        if(EXISTS "${_MIMALLOC_LIB_PATH}")
            message(STATUS "Detected mimalloc archive: ${_MIMALLOC_LIB_PATH}")
            # Extract version from FetchContent declaration above (v2.1.7)
            set(MIMALLOC_VERSION "2.1.7")
            message(STATUS "Using cached ${BoldGreen}mimalloc${ColorReset}, version ${BoldGreen}${MIMALLOC_VERSION}${ColorReset}: ${BoldCyan}${_MIMALLOC_LIB_PATH}${ColorReset}")

            # Create an imported target for the cached library
            add_library(mimalloc-static STATIC IMPORTED)
            set_target_properties(mimalloc-static PROPERTIES
                IMPORTED_LOCATION "${_MIMALLOC_LIB_PATH}"
                INTERFACE_INCLUDE_DIRECTORIES "${MIMALLOC_SOURCE_DIR}/include"
            )

            # Propagate cached static library location to downstream consumers
            set(MIMALLOC_LIBRARIES "${_MIMALLOC_LIB_PATH}")
            if(NOT ASCIICHAT_MIMALLOC_LINK_LIB)
                set(ASCIICHAT_MIMALLOC_LINK_LIB "${_MIMALLOC_LIB_PATH}")
            endif()

            # Also create mimalloc-shared target for shared library builds
            # Determine the shared library file path
            get_filename_component(_MIMALLOC_LIB_DIR "${_MIMALLOC_LIB_PATH}" DIRECTORY)
            get_filename_component(_MIMALLOC_LIB_NAME "${_MIMALLOC_LIB_PATH}" NAME_WE)
            # Replace "mimalloc" with "mimalloc-shared" in the filename
            string(REPLACE "libmimalloc" "libmimalloc-shared" _MIMALLOC_SHARED_NAME "${_MIMALLOC_LIB_NAME}")
            set(_MIMALLOC_SHARED_LIB_PATH "${_MIMALLOC_LIB_DIR}/${_MIMALLOC_SHARED_NAME}.a")

            if(EXISTS "${_MIMALLOC_SHARED_LIB_PATH}")
                add_library(mimalloc-shared STATIC IMPORTED)
                set_target_properties(mimalloc-shared PROPERTIES
                    IMPORTED_LOCATION "${_MIMALLOC_SHARED_LIB_PATH}"
                    INTERFACE_INCLUDE_DIRECTORIES "${MIMALLOC_SOURCE_DIR}/include"
                )
                set(MIMALLOC_SHARED_LIBRARIES "${_MIMALLOC_SHARED_LIB_PATH}")
                if(NOT ASCIICHAT_MIMALLOC_SHARED_LINK_LIB)
                    set(ASCIICHAT_MIMALLOC_SHARED_LINK_LIB "${_MIMALLOC_SHARED_LIB_PATH}")
                endif()
            endif()

            # Skip FetchContent download/build
            set(_MIMALLOC_CACHED TRUE)
        else()
            # Download and build mimalloc from GitHub
            message(STATUS "Downloading ${BoldYellow}mimalloc${ColorReset} from GitHub...")

            include(FetchContent)

            FetchContent_Declare(
                mimalloc
                GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
                GIT_TAG v2.1.7  # Latest stable v2.x release
                GIT_SHALLOW TRUE
                # Use persistent cache directories (survives build/ deletion)
                SOURCE_DIR "${MIMALLOC_SOURCE_DIR}"
                BINARY_DIR "${MIMALLOC_BUILD_DIR}"
            )

            set(_MIMALLOC_CACHED FALSE)
        endif()
    endif()

    # Only configure and build if not using cached library and not from vcpkg
    if(NOT _MIMALLOC_CACHED AND NOT _MIMALLOC_FROM_VCPKG)
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

        # Enable AddressSanitizer support for Debug builds
        # This builds libmimalloc-asan-debug.a which is compatible with programs using -fsanitize=address
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(MI_TRACK_ASAN ON CACHE BOOL "Build mimalloc with AddressSanitizer support")
            message(STATUS "Enabling ${BoldCyan}MI_TRACK_ASAN${ColorReset} for AddressSanitizer compatibility")

            # On macOS, MI_OVERRIDE must be OFF when using AddressSanitizer
            # The address sanitizer redirects malloc/free, so mimalloc override conflicts
            if(APPLE)
                set(MI_OVERRIDE OFF CACHE BOOL "Disable override on macOS with ASAN" FORCE)
                message(STATUS "Disabled ${BoldCyan}MI_OVERRIDE${ColorReset} on macOS (required for AddressSanitizer)")
            endif()
        endif()

        # Platform-specific mimalloc options
        if(WIN32)
            set(MI_WIN_REDIRECT ON CACHE BOOL "Redirect malloc on Windows")
        endif()

        # For musl builds, use local dynamic TLS to avoid conflicts
        if(USE_MUSL)
            set(MI_LOCAL_DYNAMIC_TLS ON CACHE BOOL "Use local dynamic TLS for musl compatibility")
            message(STATUS "Enabling ${BoldCyan}MI_LOCAL_DYNAMIC_TLS${ColorReset} for musl compatibility")
        endif()

        # Save current output directory settings
        set(_SAVED_ARCHIVE_OUTPUT_DIR ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
        set(_SAVED_LIBRARY_OUTPUT_DIR ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

        # Set output directories to persistent cache (so library survives build/ deletion)
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${MIMALLOC_BUILD_DIR}/lib")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${MIMALLOC_BUILD_DIR}/lib")

        # Make mimalloc sources available (modern CMake approach)
        FetchContent_MakeAvailable(mimalloc)

        # Restore original output directory settings
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_SAVED_ARCHIVE_OUTPUT_DIR})
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_SAVED_LIBRARY_OUTPUT_DIR})

        # Manually create mimalloc targets with different compile flags
        # Use static.c which includes all other source files (single compilation unit)
        set(MIMALLOC_SRCS "${MIMALLOC_SOURCE_DIR}/src/static.c")

        # Common compile options for both targets
        set(MIMALLOC_COMMON_OPTIONS
            -include errno.h  # C23 compatibility
            -Wno-undef       # Suppress mimalloc warnings
            -Wno-strict-prototypes
        )

        # Target 1: mimalloc-static (for executables, uses global -fPIE)
        add_library(mimalloc-static STATIC ${MIMALLOC_SRCS})
        target_include_directories(mimalloc-static PUBLIC
            "${MIMALLOC_SOURCE_DIR}/include"
        )
        target_compile_options(mimalloc-static PRIVATE ${MIMALLOC_COMMON_OPTIONS})

        # Set output name based on build type and ASAN support
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_MIMALLOC_OUTPUT_NAME "mimalloc-asan-debug")
        else()
            set(_MIMALLOC_OUTPUT_NAME "mimalloc")
        endif()

        set_target_properties(mimalloc-static PROPERTIES
            OUTPUT_NAME "${_MIMALLOC_OUTPUT_NAME}"
            ARCHIVE_OUTPUT_DIRECTORY "${MIMALLOC_BUILD_DIR}/lib"
        )

        # Target 2: mimalloc-shared (for shared library, uses -fPIC and global-dynamic TLS on Unix)
        add_library(mimalloc-shared STATIC ${MIMALLOC_SRCS})
        target_include_directories(mimalloc-shared PUBLIC
            "${MIMALLOC_SOURCE_DIR}/include"
        )
        target_compile_options(mimalloc-shared PRIVATE
            ${MIMALLOC_COMMON_OPTIONS}
        )
        # Unix-specific flags for shared library compatibility
        if(NOT WIN32)
            target_compile_options(mimalloc-shared PRIVATE
                -fno-pie                    # Disable PIE
                -fPIC                       # Enable PIC for shared library
                -ftls-model=global-dynamic  # Correct TLS model for shared libraries
            )
        endif()

        # Set output name for shared library variant (also uses ASAN naming in Debug)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_MIMALLOC_SHARED_OUTPUT_NAME "mimalloc-shared-asan-debug")
        else()
            set(_MIMALLOC_SHARED_OUTPUT_NAME "mimalloc-shared")
        endif()

        set_target_properties(mimalloc-shared PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            OUTPUT_NAME "${_MIMALLOC_SHARED_OUTPUT_NAME}"
            ARCHIVE_OUTPUT_DIRECTORY "${MIMALLOC_BUILD_DIR}/lib"
        )

        # For musl builds, set REALGCC environment for both targets
        if(USE_MUSL AND REAL_GCC)
            set_target_properties(mimalloc-static PROPERTIES
                RULE_LAUNCH_COMPILE "env REALGCC=${REAL_GCC}"
            )
            set_target_properties(mimalloc-shared PROPERTIES
                RULE_LAUNCH_COMPILE "env REALGCC=${REAL_GCC}"
            )
            message(STATUS "Configured ${BoldGreen}mimalloc${ColorReset} build environment with REALGCC=${BoldCyan}${REAL_GCC}${ColorReset}")
        endif()

        # Update status messages based on ASAN support
        set(MIMALLOC_VERSION "2.1.7")
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            message(STATUS "Created two ${BoldGreen}mimalloc${ColorReset} targets with ${BoldCyan}AddressSanitizer${ColorReset} support:")
            message(STATUS "  - ${BoldCyan}mimalloc-static${ColorReset} → ${BoldBlue}${_MIMALLOC_OUTPUT_NAME}.a${ColorReset}: for executables (uses -fPIE from global flags)")
            message(STATUS "  - ${BoldCyan}mimalloc-shared${ColorReset} → ${BoldBlue}${_MIMALLOC_SHARED_OUTPUT_NAME}.a${ColorReset}: for shared libraries (uses -fPIC and global-dynamic TLS)")
            message(STATUS "Built ${BoldGreen}mimalloc${ColorReset} from source, version ${BoldGreen}${MIMALLOC_VERSION}${ColorReset}, with ${BoldCyan}ASAN${ColorReset} tracking enabled")
        else()
            message(STATUS "Created two ${BoldGreen}mimalloc${ColorReset} targets:")
            message(STATUS "  - ${BoldCyan}mimalloc-static${ColorReset}: for executables (uses -fPIE from global flags)")
            message(STATUS "  - ${BoldCyan}mimalloc-shared${ColorReset}: for shared libraries (uses -fPIC and global-dynamic TLS)")
            message(STATUS "Built ${BoldGreen}mimalloc${ColorReset} from source, version ${BoldGreen}${MIMALLOC_VERSION}${ColorReset}")
        endif()

        # Only set these for FetchContent builds (vcpkg already set MIMALLOC_LIBRARIES)
        set(MIMALLOC_LIBRARIES "${_MIMALLOC_LIB_PATH}")
        set(ASCIICHAT_MIMALLOC_LINK_LIB "${_MIMALLOC_LIB_PATH}")
        if(EXISTS "${_MIMALLOC_SHARED_LIB_PATH}")
            set(MIMALLOC_SHARED_LIBRARIES "${_MIMALLOC_SHARED_LIB_PATH}")
            set(ASCIICHAT_MIMALLOC_SHARED_LINK_LIB "${_MIMALLOC_SHARED_LIB_PATH}")
        endif()
    endif()

    if(NOT ASCIICHAT_MIMALLOC_LINK_LIB)
        set(_ascii_chat_mimalloc_candidates
            "${_MIMALLOC_LIB_PATH}"
            "${MIMALLOC_BUILD_DIR}/lib/libmimalloc.a"
            "${MIMALLOC_BUILD_DIR}/lib/libmimalloc-static.a"
            "${MIMALLOC_BUILD_DIR}/lib/mimalloc-static.lib"
            "${MIMALLOC_BUILD_DIR}/lib/mimalloc.lib"
        )
        foreach(candidate IN LISTS _ascii_chat_mimalloc_candidates)
            if(candidate AND EXISTS "${candidate}")
                set(ASCIICHAT_MIMALLOC_LINK_LIB "${candidate}")
                break()
            endif()
        endforeach()
    endif()

    # Define USE_MIMALLOC for all source files so they can use mi_malloc/mi_free directly
    # Also define MI_STATIC_LIB to tell mimalloc headers we're using static library (prevents dllimport)
    add_compile_definitions(USE_MIMALLOC MI_STATIC_LIB)

    if(MIMALLOC_LIBRARIES)
        message(STATUS "mimalloc static library resolved to: ${MIMALLOC_LIBRARIES}")
    endif()
    if(MIMALLOC_SHARED_LIBRARIES)
        message(STATUS "mimalloc shared library resolved to: ${MIMALLOC_SHARED_LIBRARIES}")
    endif()

    message(STATUS "${BoldGreen}mimalloc${ColorReset} will override malloc/free globally")
    message(STATUS "Your existing SAFE_MALLOC macros will automatically use ${BoldGreen}mimalloc${ColorReset}")
endif()
