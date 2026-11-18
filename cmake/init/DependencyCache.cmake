# Select a platform root before adding the build configuration. Explicit roots
# passed by parent builds or container mounts remain authoritative.
if(NOT DEFINED ASCIICHAT_DEPS_CACHE_ROOT OR ASCIICHAT_DEPS_CACHE_ROOT STREQUAL "")
    if(DEFINED ENV{DOCKER_OS})
        set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache/docker/$ENV{DOCKER_OS}" CACHE PATH "Root directory for dependency cache (Docker $ENV{DOCKER_OS})" FORCE)
    elseif(EXISTS "/.dockerenv")
        message(WARNING "Running in Docker without DOCKER_OS set - using generic 'docker' cache path. Set DOCKER_OS=amd64 or DOCKER_OS=arm64 to avoid cross-arch conflicts.")
        set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache/docker" CACHE PATH "Root directory for dependency cache (Docker)" FORCE)
    elseif(UNIX AND EXISTS "/proc/version")
        file(READ "/proc/version" _asciichat_proc_version)
        if(_asciichat_proc_version MATCHES "[Mm]icrosoft|[Ww][Ss][Ll]")
            # WSL_DISTRO_NAME is supplied by WSL and preserves the name passed
            # to `wsl.exe -d`. Keep each distro's dependency binaries isolated.
            if(DEFINED ENV{WSL_DISTRO_NAME} AND NOT "$ENV{WSL_DISTRO_NAME}" STREQUAL "")
                set(_asciichat_wsl_distro "$ENV{WSL_DISTRO_NAME}")
            elseif(EXISTS "/etc/os-release")
                file(STRINGS "/etc/os-release" _asciichat_os_release REGEX "^ID=")
                string(REGEX REPLACE "^ID=(.*)$" "\\1" _asciichat_wsl_distro "${_asciichat_os_release}")
                string(REPLACE "\"" "" _asciichat_wsl_distro "${_asciichat_wsl_distro}")
            endif()

            if(NOT _asciichat_wsl_distro)
                set(_asciichat_wsl_distro "unknown")
            endif()
            string(REGEX REPLACE "[^A-Za-z0-9._-]" "_" _asciichat_wsl_distro "${_asciichat_wsl_distro}")
            set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache/wsl/${_asciichat_wsl_distro}" CACHE PATH "Root directory for dependency cache (WSL ${_asciichat_wsl_distro})" FORCE)
            message(STATUS "WSL distribution dependency cache: ${ASCIICHAT_DEPS_CACHE_ROOT}")
        else()
            set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache" CACHE PATH "Root directory for dependency cache (native)" FORCE)
        endif()
    else()
        set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache" CACHE PATH "Root directory for dependency cache (native)" FORCE)
    endif()
endif()

# Keep source and binary trees together under the build configuration. FetchContent
# creates both kinds of tree, so its base directory cannot be a shared source-only
# directory without mixing Debug and Release artifacts.
cmake_path(ABSOLUTE_PATH ASCIICHAT_DEPS_CACHE_ROOT NORMALIZE)
if(DEFINED ASCIICHAT_DEPS_CACHE_DIR AND NOT ASCIICHAT_DEPS_CACHE_DIR STREQUAL "")
    cmake_path(ABSOLUTE_PATH ASCIICHAT_DEPS_CACHE_DIR NORMALIZE)
endif()

# Older configure commands used the cache root as the configuration directory.
# Migrate that value while retaining explicit, separate dependency directories
# passed by parent/auxiliary builds.
if(NOT DEFINED ASCIICHAT_DEPS_CACHE_DIR OR ASCIICHAT_DEPS_CACHE_DIR STREQUAL ""
        OR ASCIICHAT_DEPS_CACHE_DIR STREQUAL ASCIICHAT_DEPS_CACHE_ROOT)
    set(ASCIICHAT_DEPS_CACHE_DIR "${ASCIICHAT_DEPS_CACHE_ROOT}/${CMAKE_BUILD_TYPE}")
endif()
set(ASCIICHAT_DEPS_CACHE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}" CACHE PATH
    "Dependency cache for current build type" FORCE)
string(REGEX REPLACE "[^A-Za-z0-9_.-]" "-" _asciichat_generator_cache "${CMAKE_GENERATOR}")
set(_asciichat_compiler_cache "${CMAKE_C_COMPILER_ID}")
if(NOT _asciichat_compiler_cache)
    if(CMAKE_C_COMPILER MATCHES "(^|[/\\\\])clang(-cl)?(\.exe)?$")
        set(_asciichat_compiler_cache "Clang")
    elseif(CMAKE_C_COMPILER MATCHES "(^|[/\\\\])(gcc|cc)(-[0-9]+)?(\.exe)?$")
        set(_asciichat_compiler_cache "GCC")
    elseif(CMAKE_C_COMPILER MATCHES "(^|[/\\\\])cl(\.exe)?$")
        set(_asciichat_compiler_cache "MSVC")
    else()
        set(_asciichat_compiler_cache "default")
    endif()
endif()
string(REGEX REPLACE "[^A-Za-z0-9_.-]" "-" _asciichat_compiler_cache "${_asciichat_compiler_cache}")
set(FETCHCONTENT_BASE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/fetchcontent-${_asciichat_compiler_cache}-${_asciichat_generator_cache}"
    CACHE PATH
    "FetchContent source/build cache for current build type" FORCE)
