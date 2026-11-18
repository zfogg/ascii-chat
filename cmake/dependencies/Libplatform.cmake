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

if(USE_MUSL OR NOT BUILD_SHARED_LIBS)
    set(_ascii_chat_libplatform_shared OFF)
elseif(BUILD_SHARED_LIBS)
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

if(NOT TARGET utf8proc)
    add_library(utf8proc STATIC
        "${_ascii_chat_repo_root}/deps/ascii-chat-deps/utf8proc/utf8proc.c")
    target_include_directories(utf8proc PUBLIC
        "${_ascii_chat_repo_root}/deps/ascii-chat-deps/utf8proc")
    target_compile_definitions(utf8proc PUBLIC UTF8PROC_STATIC=1)
    set_target_properties(utf8proc PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        POSITION_INDEPENDENT_CODE ON)
endif()
set(LIBPLATFORM_UTF8PROC_TARGET utf8proc CACHE STRING
    "utf8proc target shared with ascii-chat" FORCE)

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

unset(_ascii_chat_libplatform_shared)
unset(_ascii_chat_libplatform_source)
unset(_ascii_chat_libplatform_build)
unset(_ascii_chat_repo_root)
