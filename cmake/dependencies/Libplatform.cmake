# Configure the extracted libplatform dependency.
#
# Development builds use the shared library for fast iteration. Release-style
# builds use the static library unless the caller explicitly requests shared
# dependencies. musl is always static because its release toolchain is static.

# This file is included by both the top-level native project and the
# standalone src/web WASM project. CMAKE_SOURCE_DIR is src/web in the latter,
# so resolve paths from this file instead of assuming the caller is the repo
# root.
get_filename_component(_ascii_chat_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(USE_MUSL OR ASCIICHAT_STATIC_DEPS)
    set(_ascii_chat_libplatform_shared OFF)
elseif(ASCIICHAT_SHARED_DEPS)
    set(_ascii_chat_libplatform_shared ON)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
    set(_ascii_chat_libplatform_shared ON)
else()
    set(_ascii_chat_libplatform_shared OFF)
endif()

set(LIBPLATFORM_BUILD_SHARED "${_ascii_chat_libplatform_shared}" CACHE BOOL
    "Build libplatform as a shared library" FORCE)
set(LIBPLATFORM_BUILD_TESTS OFF CACHE BOOL
    "Disable standalone libplatform tests in ascii-chat" FORCE)

# Use the checked-out submodule directly.  Reusing a cached source tree can
# mix headers and implementations from different libplatform revisions.
set(_ascii_chat_libplatform_source "${_ascii_chat_repo_root}/deps/ascii-chat-deps/libplatform")
set(_ascii_chat_libplatform_build "${ASCIICHAT_DEPS_CACHE_DIR}/libplatform-build")
if(NOT TARGET libplatform)
    add_subdirectory(
        "${_ascii_chat_libplatform_source}"
        "${_ascii_chat_libplatform_build}"
        EXCLUDE_FROM_ALL)
endif()

# Older libplatform revisions list the standalone POSIX signal module beside
# system.c, but both define platform_install_crash_handler with incompatible
# APIs. ascii-chat uses the system implementation; remove the duplicate when
# that source is present in the checked-out dependency.
if(TARGET libplatform-objects)
    get_target_property(_ascii_chat_libplatform_sources libplatform-objects SOURCES)
    list(FILTER _ascii_chat_libplatform_sources EXCLUDE REGEX "(^|/)(signal|backtrace)\\.c$")
    set_property(TARGET libplatform-objects PROPERTY SOURCES
        "${_ascii_chat_libplatform_sources}")
    set_property(SOURCE
        "${_ascii_chat_libplatform_source}/lib/posix/filesystem.c"
        TARGET_DIRECTORY libplatform-objects
        APPEND PROPERTY COMPILE_DEFINITIONS
        platform_get_data_dir_candidates=libplatform_filesystem_get_data_dir_candidates)
endif()

if(TARGET asciichat-project-includes)
    target_include_directories(asciichat-project-includes INTERFACE
        "${_ascii_chat_libplatform_source}/lib")
endif()

# libplatform owns the shared path/logging helpers, so give it the checkout
# root that CMake already knows instead of letting it fall back to ".".
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_ENABLE_COVERAGE)
    set(_ascii_chat_source_root ".")
else()
    string(REPLACE "\\" "/" _ascii_chat_source_root "${CMAKE_SOURCE_DIR}")
endif()
target_compile_definitions(libplatform PRIVATE
    LIBPLATFORM_SOURCE_ROOT="${_ascii_chat_source_root}")
target_include_directories(libplatform PRIVATE
    ${_ascii_chat_repo_root}/include
    ${_ascii_chat_repo_root}/deps
    ${_ascii_chat_repo_root}/deps/ascii-chat-deps/yyjson/src
    ${_ascii_chat_repo_root}/deps/ascii-chat-deps/utf8proc)
if(TARGET libplatform-objects)
    target_include_directories(libplatform-objects PRIVATE
        ${_ascii_chat_repo_root}/include
        ${_ascii_chat_repo_root}/deps)
endif()
if(LIBSODIUM_INCLUDE_DIRS)
    target_include_directories(libplatform PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
endif()
if(EMSCRIPTEN)
    target_compile_options(libplatform PRIVATE -pthread -matomics -mbulk-memory)
endif()


# libplatform's POSIX helpers use interfaces such as fileno() that musl hides
# from strict C mode unless the feature-test macros are enabled on the target.
if(UNIX AND NOT APPLE)
    target_compile_definitions(libplatform PRIVATE _GNU_SOURCE)
    # libplatform builds its sources through this object target; keep the
    # feature-test macro on that compile path as well as on the final library.
    if(TARGET libplatform-objects)
        target_compile_definitions(libplatform-objects PRIVATE _GNU_SOURCE)
    endif()
    if(DEFINED KERNEL_HEADERS_DIR AND EXISTS "${KERNEL_HEADERS_DIR}")
        target_include_directories(libplatform PRIVATE "${KERNEL_HEADERS_DIR}")
    endif()
    if(DEFINED FONTCONFIG_INCLUDE_DIRS)
        target_include_directories(libplatform PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
    endif()
    if(DEFINED LIBEXECINFO_INCLUDE_DIRS)
        target_include_directories(libplatform PRIVATE ${LIBEXECINFO_INCLUDE_DIRS})
    endif()
endif()

if(FONTCONFIG_LINK_LIBRARIES)
    target_include_directories(libplatform PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
    target_link_libraries(libplatform PUBLIC ${FONTCONFIG_LINK_LIBRARIES})
endif()

unset(_ascii_chat_libplatform_shared)
unset(_ascii_chat_libplatform_source)
unset(_ascii_chat_libplatform_build)
unset(_ascii_chat_repo_root)
unset(_ascii_chat_source_root)
