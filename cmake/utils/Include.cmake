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
    set(_asciichat_sqlite_include_dirs ${SQLITE3_INCLUDE_DIRS})
    if(USE_MUSL)
        # Dependency modules add their include variables at different points
        # in configuration, so sanitize all of them before subdirectories add
        # their private include paths to concrete targets.
        get_cmake_property(_asciichat_include_variables VARIABLES)
        foreach(_asciichat_include_variable IN LISTS _asciichat_include_variables)
            if(_asciichat_include_variable MATCHES "_INCLUDE_DIRS$")
                set(_asciichat_include_values ${${_asciichat_include_variable}})
                list(FILTER _asciichat_include_values EXCLUDE REGEX
                    "^-I/usr/include([^ ]*)?$|^/usr/include$|^/usr/include/.*-linux-gnu(/|$)")
                # Keep the sanitized value visible to the parent directory and
                # to subdirectories that add these paths to concrete targets.
                set(${_asciichat_include_variable} "${_asciichat_include_values}" PARENT_SCOPE)
            endif()
        endforeach()
        # FindSQLite3 may report the host pkg-config include root.  Do not
        # let that root reintroduce glibc ahead of the musl headers.
        list(FILTER _asciichat_sqlite_include_dirs EXCLUDE REGEX
            "^-I/usr/include([^ ]*)?$|^/usr/include$|^/usr/include/.*-linux-gnu(/|$)")
    endif()
    # Base include directories (source tree). Panic-instrumentation-enabled builds prepend
    # instrumented include directories later during ascii_panic_finalize().
    # Note: All installable dependencies are in deps/ascii-chat-deps/
    target_include_directories(asciichat-project-includes INTERFACE
        ${CMAKE_BINARY_DIR}/generated # Configuration-generated public headers
        ${CMAKE_SOURCE_DIR}/include  # Public API headers
        ${CMAKE_SOURCE_DIR}/lib      # Private implementation headers (if any remain)
        ${CMAKE_SOURCE_DIR}/src      # Application headers
        ${_asciichat_sqlite_include_dirs} # Public discovery/database declarations
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
        # Don't add system include paths when using musl - musl-gcc handles this via -specs
        if(NOT USE_MUSL)
            # Use pkg-config flags (matches Makefile CFLAGS approach)
            add_core_deps_compile_flags()
            get_core_deps_include_dirs(CORE_INCLUDE_DIRS)
            target_include_directories(asciichat-project-includes INTERFACE ${CORE_INCLUDE_DIRS})
        else()
            # pkg-config commonly reports the host libc as -I/usr/include.  A
            # musl target must not inherit that path: clang would then select
            # glibc headers before the target's musl headers.
            foreach(DEP IN LISTS ASCIICHAT_CORE_DEPS)
                set(_dep_include_var "${DEP}_INCLUDE_DIRS")
                if(DEFINED ${_dep_include_var} AND NOT "${${_dep_include_var}}" STREQUAL "")
                    set(_dep_includes ${${_dep_include_var}})
                    list(FILTER _dep_includes EXCLUDE REGEX "^-I/usr/include$|^-I/usr/include/.*-linux-gnu(/|$)|^/usr/include$|^/usr/include/.*-linux-gnu(/|$)")
                    set(${_dep_include_var} "${_dep_includes}" PARENT_SCOPE)
                endif()
                if(TARGET PkgConfig::${DEP})
                    get_target_property(_pkg_includes PkgConfig::${DEP} INTERFACE_INCLUDE_DIRECTORIES)
                    if(_pkg_includes)
                        list(FILTER _pkg_includes EXCLUDE REGEX "^/usr/include$|^/usr/include/.*-linux-gnu(/|$)")
                        set_property(TARGET PkgConfig::${DEP} PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_pkg_includes}")
                    endif()
                    get_target_property(_pkg_compile_options PkgConfig::${DEP} INTERFACE_COMPILE_OPTIONS)
                    if(_pkg_compile_options)
                        list(FILTER _pkg_compile_options EXCLUDE REGEX "^-I/usr/include$|^-I/usr/include/.*-linux-gnu(/|$)")
                        set_property(TARGET PkgConfig::${DEP} PROPERTY INTERFACE_COMPILE_OPTIONS "${_pkg_compile_options}")
                    endif()
                endif()
                set(_cflags_var "${DEP}_CFLAGS_OTHER")
                if(DEFINED ${_cflags_var} AND NOT "${${_cflags_var}}" STREQUAL "")
                    set(_musl_cflags ${${_cflags_var}})
                    list(FILTER _musl_cflags EXCLUDE REGEX "^-I/usr/include$|^-I/usr/include/.*-linux-gnu(/|$)")
                    target_compile_options(asciichat-project-options INTERFACE ${_musl_cflags})
                endif()
            endforeach()
            # When using musl, strip /usr/include from all include paths
            get_core_deps_include_dirs(CORE_INCLUDE_DIRS)
            list(FILTER CORE_INCLUDE_DIRS EXCLUDE REGEX "^/usr/include$|^/usr/include/.*-linux-gnu(/|$)")

            # Strip /usr/include from BearSSL as well
            if(BEARSSL_INCLUDE_DIRS)
                list(FILTER BEARSSL_INCLUDE_DIRS EXCLUDE REGEX "^/usr/include$|^/usr/include/.*-linux-gnu(/|$)")
            endif()

            # Add the musl-built library include paths
            target_include_directories(asciichat-project-includes INTERFACE
                ${CORE_INCLUDE_DIRS}
                ${BEARSSL_INCLUDE_DIRS}
            )
        endif()
    endif()

    if(USE_MUSL)
        # Put the target libc on the compiler's primary include search path.
        # This must be in CMAKE_*_FLAGS (before target-transitive includes),
        # because some imported pkg-config targets still emit -I/usr/include.
        # Also remove host include flags inherited from the runner environment;
        # an explicit -I/usr/include wins over the compiler's target search
        # paths even when -nostdlibinc is active.
        string(REGEX REPLACE "(^| )-I/usr/include([^ ]*)?( |$)" " " CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
        string(REGEX REPLACE "(^| )-I/usr/include([^ ]*)?( |$)" " " CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(APPEND CMAKE_C_FLAGS " -I${MUSL_INCLUDE_DIR}")
        string(APPEND CMAKE_CXX_FLAGS " -I${MUSL_INCLUDE_DIR}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "C compiler flags" FORCE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" CACHE STRING "C++ compiler flags" FORCE)
        # Keep the target libc ahead of any transitive pkg-config include
        # roots (some imported targets still expose /usr/include).
        target_include_directories(asciichat-project-includes BEFORE INTERFACE
            "${MUSL_INCLUDE_DIR}")
        target_include_directories(asciichat-project-includes SYSTEM INTERFACE
            ${KERNEL_HEADERS_DIR} ${LIBEXECINFO_INCLUDE_DIRS}
            "$<$<COMPILE_LANGUAGE:CXX>:${ALPINE_LIBCXX_INCLUDE_DIR}>")
    endif()

    # Dependency roots (including mimalloc's vcpkg root) may also contain an
    # older FFmpeg. Its public structs must match the selected libraries, so
    # use a normal include path before those roots, not a trailing SYSTEM path.
    target_include_directories(asciichat-project-includes BEFORE INTERFACE ${FFMPEG_INCLUDE_DIRS})
endfunction()
