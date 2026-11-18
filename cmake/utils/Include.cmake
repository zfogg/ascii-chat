# =============================================================================
# Include Directories Configuration Module
# =============================================================================
# This module configures include directories for the project.
# Handles base directories, dependency includes, and platform-specific paths.
#
# Prerequisites:
#   - Must run after Dependencies.cmake (for ZSTD_INCLUDE_DIRS, etc.)
#   - Must run after Mimalloc.cmake (for USE_MIMALLOC and FETCHCONTENT_BASE_DIR)
#   - Must run after Dependencies.cmake (for dependency interface targets)
#
# Outputs:
#   - Base include directories added (lib/, src/)
#   - Dependency include directories added (platform-specific)
#   - Mimalloc include directory added if USE_MIMALLOC is ON
# =============================================================================

function(configure_include_directories)
    # Base include directories (source tree). Panic-instrumentation-enabled builds prepend
    # instrumented include directories later during ascii_panic_finalize().
    # Note: All installable dependencies are in deps/ascii-chat-deps/
    target_include_directories(asciichat-project-includes INTERFACE
        ${CMAKE_BINARY_DIR}/generated # Configuration-generated public headers
        ${CMAKE_SOURCE_DIR}/include  # Public API headers
        ${CMAKE_SOURCE_DIR}/include/ascii-chat  # Legacy short subsystem includes
        ${CMAKE_SOURCE_DIR}/deps/recast/include # Standalone Recast marker
        ${CMAKE_SOURCE_DIR}/lib      # Private implementation headers (if any remain)
        ${CMAKE_SOURCE_DIR}/src      # Application headers
        ${CMAKE_SOURCE_DIR}/src/common # Co-located ascii-chat option headers
        ${libflags_SOURCE_DIR}/include # Public libflags headers for tooling and consumers
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
        target_link_libraries(asciichat-project-includes INTERFACE asciichat-core-dependencies)

    else()
        # Don't add system include paths when using musl; the Clang target
        # toolchain supplies the selected musl include roots.
        if(NOT USE_MUSL)
            # Use pkg-config flags (matches Makefile CFLAGS approach)
            target_link_libraries(asciichat-project-includes INTERFACE asciichat-core-dependencies)
        else()
            # pkg-config commonly reports the host libc as -I/usr/include.  A
            # musl target must not inherit that path: clang would then select
            # glibc headers before the target's musl headers.
            # When using musl, strip /usr/include from all include paths
            get_target_property(_asciichat_core_includes asciichat-core-dependencies INTERFACE_INCLUDE_DIRECTORIES)
            list(FILTER _asciichat_core_includes EXCLUDE REGEX "^-I/usr/include([^ ]*)?$|^/usr/include$|^/usr/include/.*-linux-gnu(/|$)")
            get_target_property(CORE_COMPILE_OPTIONS asciichat-core-dependencies INTERFACE_COMPILE_OPTIONS)
            if(CORE_COMPILE_OPTIONS)
                list(FILTER CORE_COMPILE_OPTIONS EXCLUDE REGEX "^-I/usr/include$|^-I/usr/include/.*-linux-gnu(/|$)")
                set_property(TARGET asciichat-core-dependencies PROPERTY INTERFACE_COMPILE_OPTIONS "${CORE_COMPILE_OPTIONS}")
            endif()
            target_link_libraries(asciichat-project-includes INTERFACE asciichat-core-dependencies)

            # Dependency include directories are carried by their imported
            # targets; do not copy legacy include variables into consumers.
        endif()
    endif()

    if(USE_MUSL)
        # Put the target libc on the compiler's primary include search path.
        # This must be in CMAKE_*_FLAGS (before target-transitive includes),
        # because some imported pkg-config targets still emit -I/usr/include.
        # Also remove host include flags inherited from the runner environment;
        # an explicit -I/usr/include wins over the compiler's target search
        # paths even when -nostdlibinc is active.
        string(REGEX REPLACE "(^| )(-I|-isystem[ ]+)/usr/include([^ ]*)?( |$)" " " CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
        string(REGEX REPLACE "(^| )(-I|-isystem[ ]+)/usr/include([^ ]*)?( |$)" " " CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(APPEND CMAKE_C_FLAGS " -isystem ${MUSL_INCLUDE_DIR}")
        string(APPEND CMAKE_CXX_FLAGS " -isystem ${MUSL_INCLUDE_DIR}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "C compiler flags" FORCE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" CACHE STRING "C++ compiler flags" FORCE)
        # Keep the target libc ahead of any transitive pkg-config include
        # roots (some imported targets still expose /usr/include).
        target_include_directories(asciichat-project-includes BEFORE INTERFACE
            "${MUSL_INCLUDE_DIR}")
        target_include_directories(asciichat-project-includes SYSTEM INTERFACE
            ${KERNEL_HEADERS_DIR})
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_include_directories(asciichat-project-includes SYSTEM INTERFACE
                "$<$<COMPILE_LANGUAGE:CXX>:${ALPINE_LIBCXX_INCLUDE_DIR}>")
        endif()
    endif()

    # Dependency roots (including mimalloc's vcpkg root) may also contain an
    # older FFmpeg. Its public structs must match the selected libraries, so
    # use a normal include path before those roots, not a trailing SYSTEM path.
    if(TARGET asciichat-ffmpeg)
        get_target_property(_ffmpeg_includes asciichat-ffmpeg INTERFACE_INCLUDE_DIRECTORIES)
        if(_ffmpeg_includes)
            target_include_directories(asciichat-project-includes BEFORE INTERFACE ${_ffmpeg_includes})
        endif()
    endif()
endfunction()

# Dependency variables are also consumed directly by lib/CMakeLists.txt after
# configure_include_directories() returns.  Perform one final property-level
# cleanup after all native targets have been declared so late target-specific
# pkg-config paths cannot reintroduce the host glibc headers into musl builds.
function(asciichat_sanitize_musl_include_properties)
    if(NOT USE_MUSL)
        return()
    endif()

    get_directory_property(_asciichat_directory_includes INCLUDE_DIRECTORIES)
    list(FILTER _asciichat_directory_includes EXCLUDE REGEX
        "^-I/usr/include$|^-I/usr/include/.*-linux-gnu(/|$)|^/usr/include$|^/usr/include/.*-linux-gnu(/|$)")
    set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "${_asciichat_directory_includes}")

    get_property(_asciichat_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_asciichat_target IN LISTS _asciichat_targets)
        foreach(_asciichat_property IN ITEMS INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
            get_target_property(_asciichat_includes "${_asciichat_target}" "${_asciichat_property}")
            if(_asciichat_includes AND NOT _asciichat_includes STREQUAL "_asciichat_includes-NOTFOUND")
                list(FILTER _asciichat_includes EXCLUDE REGEX
                    "^-I/usr/include$|^-I/usr/include/.*-linux-gnu(/|$)|^/usr/include$|^/usr/include/.*-linux-gnu(/|$)")
                set_property(TARGET "${_asciichat_target}" PROPERTY
                    "${_asciichat_property}" "${_asciichat_includes}")
            endif()
        endforeach()
    endforeach()
endfunction()
