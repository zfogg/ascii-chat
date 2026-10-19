# =============================================================================
# Include Directories Configuration Module
# =============================================================================
# This module configures include directories for the project.
# Handles base directories, dependency includes, and platform-specific paths.
#
# Prerequisites:
#   - Must run after Dependencies.cmake (for ZSTD_INCLUDE_DIRS, etc.)
#   - Must run after Mimalloc.cmake (for USE_MIMALLOC and FETCHCONTENT_BASE_DIR)
#   - Must run after CoreDependencies.cmake (for helper functions)
#
# Outputs:
#   - Base include directories added (lib/, src/)
#   - Dependency include directories added (platform-specific)
#   - Mimalloc include directory added if USE_MIMALLOC is ON
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/CoreDependencies.cmake)

function(configure_include_directories)
    # Base include directories (source tree). Panic-instrumentation-enabled builds prepend
    # instrumented include directories later during ascii_panic_finalize().
    # Note: All installable dependencies are in deps/ascii-chat-deps/
    target_include_directories(asciichat-project-includes INTERFACE
        ${CMAKE_BINARY_DIR}/generated # Configuration-generated public headers
        ${CMAKE_SOURCE_DIR}/include  # Public API headers
        ${CMAKE_SOURCE_DIR}/lib      # Private implementation headers (if any remain)
        ${CMAKE_SOURCE_DIR}/src      # Application headers
        ${SQLITE3_INCLUDE_DIRS}     # Public discovery/database declarations
    )
    # Windows: Add compat directory for POSIX header stubs (unistd.h, etc.)
    if(WIN32)
        target_include_directories(asciichat-project-includes INTERFACE ${CMAKE_SOURCE_DIR}/include/compat)
    endif()
    # deps/ must use SYSTEM to ensure -isystem (not -iquote) for angle bracket includes
    # Code uses: #include <ascii-chat-deps/uthash/src/uthash.h>
    # Also includes utf8proc: #include <ascii-chat-deps/utf8proc/utf8proc.h>
    target_include_directories(asciichat-project-includes SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/deps)

    # Add dependency include directories (matching pkg-config approach)
    if(WIN32)
        # Use triplet-specific include path (set earlier based on build type)
        if(DEFINED VCPKG_INCLUDE_PATH)
            target_include_directories(asciichat-project-includes INTERFACE ${VCPKG_INCLUDE_PATH})
        endif()

        # Add core dependencies with SYSTEM flag (avoid conflicts)
        get_core_deps_include_dirs(CORE_INCLUDE_DIRS)
        target_include_directories(asciichat-project-includes SYSTEM INTERFACE ${CORE_INCLUDE_DIRS})

        # Add mimalloc include directory for USE_MIMALLOC builds
        # Use MIMALLOC_INCLUDE_DIRS which handles system vs FetchContent mimalloc
        if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
            target_include_directories(asciichat-project-includes BEFORE INTERFACE ${MIMALLOC_INCLUDE_DIRS})
        endif()
    else()
        # Use pkg-config flags (matches Makefile CFLAGS approach)
        add_core_deps_compile_flags()

        # Don't add system include paths when using musl - musl-gcc handles this via -specs
        if(NOT USE_MUSL)
            get_core_deps_include_dirs(CORE_INCLUDE_DIRS)
            target_include_directories(asciichat-project-includes INTERFACE ${CORE_INCLUDE_DIRS})
        else()
            # When using musl, strip /usr/include from all include paths
            get_core_deps_include_dirs(CORE_INCLUDE_DIRS)
            list(REMOVE_ITEM CORE_INCLUDE_DIRS "/usr/include")

            # Strip /usr/include from BearSSL as well
            if(BEARSSL_INCLUDE_DIRS)
                list(REMOVE_ITEM BEARSSL_INCLUDE_DIRS "/usr/include")
            endif()

            # Add the musl-built library include paths
            target_include_directories(asciichat-project-includes INTERFACE
                ${CORE_INCLUDE_DIRS}
                ${BEARSSL_INCLUDE_DIRS}
            )
        endif()
    endif()

    if(USE_MUSL)
        target_include_directories(asciichat-project-includes SYSTEM INTERFACE
            ${KERNEL_HEADERS_DIR} ${LIBEXECINFO_INCLUDE_DIRS}
            "$<$<COMPILE_LANGUAGE:CXX>:${ALPINE_LIBCXX_INCLUDE_DIR}>")
    endif()

    # Dependency roots (including mimalloc's vcpkg root) may also contain an
    # older FFmpeg. Its public structs must match the selected libraries, so
    # use a normal include path before those roots, not a trailing SYSTEM path.
    target_include_directories(asciichat-project-includes BEFORE INTERFACE ${FFMPEG_INCLUDE_DIRS})
endfunction()
