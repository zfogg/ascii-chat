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
    include_directories(
        ${CMAKE_SOURCE_DIR}/include  # Public API headers
        ${CMAKE_SOURCE_DIR}/lib      # Private implementation headers (if any remain)
        ${CMAKE_SOURCE_DIR}/src      # Application headers
    )
    # deps/ must use SYSTEM to ensure -isystem (not -iquote) for angle bracket includes
    # Code uses: #include <ascii-chat-deps/uthash/src/uthash.h>
    # Also includes utf8proc: #include <ascii-chat-deps/utf8proc/utf8proc.h>
    include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/deps)

    # Add ghostty include directories (for ghostty.h)
    include_directories(SYSTEM ${GHOSTTY_INCLUDE_DIRS})

    # Add dependency include directories (matching pkg-config approach)
    if(WIN32)
        # Use triplet-specific include path (set earlier based on build type)
        if(DEFINED VCPKG_INCLUDE_PATH)
            include_directories(${VCPKG_INCLUDE_PATH})
        endif()

        # Add core dependencies with SYSTEM flag (avoid conflicts)
        # Strip /opus suffix from OPUS_INCLUDE_DIRS (macOS pkg-config quirk)
        get_core_deps_include_dirs(CORE_INCLUDE_DIRS)
        strip_suffix_from_list("${CORE_INCLUDE_DIRS}" "/opus" CORE_INCLUDE_DIRS)
        include_directories(SYSTEM ${CORE_INCLUDE_DIRS})

        # Add mimalloc include directory for USE_MIMALLOC builds
        # Use MIMALLOC_INCLUDE_DIRS which handles system vs FetchContent mimalloc
        if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
            include_directories(${MIMALLOC_INCLUDE_DIRS})
        endif()
    else()
        # Use pkg-config flags (matches Makefile CFLAGS approach)
        add_core_deps_compile_flags()

        # Don't add system include paths when using musl - musl-gcc handles this via -specs
        if(NOT USE_MUSL)
            # Strip /opus suffix from OPUS_INCLUDE_DIRS (macOS pkg-config quirk)
            get_core_deps_include_dirs(CORE_INCLUDE_DIRS)
            strip_suffix_from_list("${CORE_INCLUDE_DIRS}" "/opus" CORE_INCLUDE_DIRS)
            include_directories(${CORE_INCLUDE_DIRS})
        else()
            # When using musl, strip /usr/include from all include paths
            get_core_deps_include_dirs(CORE_INCLUDE_DIRS)
            list(REMOVE_ITEM CORE_INCLUDE_DIRS "/usr/include")
            # Also strip /opus suffix if present
            strip_suffix_from_list("${CORE_INCLUDE_DIRS}" "/opus" CORE_INCLUDE_DIRS)

            # Strip /usr/include from BearSSL as well
            if(BEARSSL_INCLUDE_DIRS)
                list(REMOVE_ITEM BEARSSL_INCLUDE_DIRS "/usr/include")
            endif()

            # Add the musl-built library include paths
            include_directories(
                ${CORE_INCLUDE_DIRS}
                ${BEARSSL_INCLUDE_DIRS}
            )
        endif()
    endif()
endfunction()

