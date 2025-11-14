# =============================================================================
# Include Directories Configuration Module
# =============================================================================
# This module configures include directories for the project.
# Handles base directories, dependency includes, and platform-specific paths.
#
# Prerequisites:
#   - Must run after Dependencies.cmake (for ZSTD_INCLUDE_DIRS, etc.)
#   - Must run after Mimalloc.cmake (for USE_MIMALLOC and FETCHCONTENT_BASE_DIR)
#
# Outputs:
#   - Base include directories added (lib/, src/)
#   - Dependency include directories added (platform-specific)
#   - Mimalloc include directory added if USE_MIMALLOC is ON
# =============================================================================

function(configure_include_directories)
    # Base include directories (source tree). Instrumentation-enabled builds prepend
    # instrumented include directories later during ascii_instrumentation_finalize().
    include_directories(
        ${CMAKE_SOURCE_DIR}/lib
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/deps/tomlc17/src
        ${CMAKE_SOURCE_DIR}/deps/uthash/src
    )

    # Add dependency include directories (matching pkg-config approach)
    if(WIN32)
        # Use triplet-specific include path (set earlier based on build type)
        if(DEFINED VCPKG_INCLUDE_PATH)
            include_directories(${VCPKG_INCLUDE_PATH})
        endif()
        # Add additional Windows include paths if found
        # Use SYSTEM for musl to avoid glibc header conflicts (-isystem vs -I)
        if(ZSTD_INCLUDE_DIRS)
            include_directories(SYSTEM ${ZSTD_INCLUDE_DIRS})
        endif()
        if(LIBSODIUM_INCLUDE_DIRS)
            include_directories(SYSTEM ${LIBSODIUM_INCLUDE_DIRS})
        endif()
        if(PORTAUDIO_INCLUDE_DIRS)
            include_directories(SYSTEM ${PORTAUDIO_INCLUDE_DIRS})
        endif()
        # Add mimalloc include directory for USE_MIMALLOC builds
        if(USE_MIMALLOC)
            include_directories(${FETCHCONTENT_BASE_DIR}/mimalloc-src/include)
        endif()
    else()
        # Use pkg-config flags (matches Makefile CFLAGS approach)
        if(ZSTD_CFLAGS_OTHER)
            add_compile_options(${ZSTD_CFLAGS_OTHER})
        endif()
        if(LIBSODIUM_CFLAGS_OTHER)
            add_compile_options(${LIBSODIUM_CFLAGS_OTHER})
        endif()
        if(PORTAUDIO_CFLAGS_OTHER)
            add_compile_options(${PORTAUDIO_CFLAGS_OTHER})
        endif()

        # Don't add system include paths when using musl - musl-gcc handles this via -specs
        if(NOT USE_MUSL)
            include_directories(
                ${ZSTD_INCLUDE_DIRS}
                ${LIBSODIUM_INCLUDE_DIRS}
                ${PORTAUDIO_INCLUDE_DIRS}
            )
        else()
            # When using musl, strip /usr/include from all include paths and add the cleaned paths
            list(REMOVE_ITEM ZSTD_INCLUDE_DIRS "/usr/include")
            list(REMOVE_ITEM LIBSODIUM_INCLUDE_DIRS "/usr/include")
            list(REMOVE_ITEM PORTAUDIO_INCLUDE_DIRS "/usr/include")
            list(REMOVE_ITEM BEARSSL_INCLUDE_DIRS "/usr/include")

            # Add the musl-built library include paths (after removing /usr/include)
            include_directories(
                ${ZSTD_INCLUDE_DIRS}
                ${LIBSODIUM_INCLUDE_DIRS}
                ${PORTAUDIO_INCLUDE_DIRS}
                ${BEARSSL_INCLUDE_DIRS}
            )
        endif()
    endif()
endfunction()

