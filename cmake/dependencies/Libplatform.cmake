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

set(LIBPLATFORM_BUILD_TESTS OFF CACHE BOOL
    "Disable standalone libplatform tests in ascii-chat" FORCE)

if(NOT TARGET asciichat-utf8proc)
    add_library(asciichat-utf8proc STATIC
        "${_ascii_chat_repo_root}/deps/ascii-chat-deps/utf8proc/utf8proc.c")
    target_include_directories(asciichat-utf8proc PUBLIC
        $<BUILD_INTERFACE:${_ascii_chat_repo_root}/deps/ascii-chat-deps/utf8proc>
        $<INSTALL_INTERFACE:include>)
    target_compile_definitions(asciichat-utf8proc PUBLIC UTF8PROC_STATIC=1)
    set_target_properties(asciichat-utf8proc PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        POSITION_INDEPENDENT_CODE ON)
endif()
set(LIBPLATFORM_UTF8PROC_TARGET asciichat-utf8proc CACHE STRING
    "utf8proc target shared with ascii-chat" FORCE)

# Use the checked-out submodule directly.  Reusing a cached source tree can
# mix headers and implementations from different libplatform revisions.
set(_ascii_chat_libplatform_source "${_ascii_chat_repo_root}/deps/ascii-chat-deps/libplatform")
set(_ascii_chat_libplatform_build "${ASCIICHAT_DEPS_CACHE_DIR}/build/libplatform")
if(NOT TARGET libplatform)
    add_subdirectory(
        "${_ascii_chat_libplatform_source}"
        "${_ascii_chat_libplatform_build}"
        EXCLUDE_FROM_ALL)
endif()

if(NOT TARGET libplatform::libplatform)
    message(FATAL_ERROR "libplatform did not provide its required libplatform::libplatform target")
endif()

if(USE_MUSL AND TARGET libplatform)
    # The upstream standalone project discovers the host pkg-config
    # fontconfig target on Linux. That would append glibc's -lfontconfig to a
    # musl link even though ascii-chat has its own musl-built archive.
    get_target_property(_libplatform_interface_links libplatform
        INTERFACE_LINK_LIBRARIES)
    if(_libplatform_interface_links)
        list(REMOVE_ITEM _libplatform_interface_links
            PkgConfig::LIBPLATFORM_FONTCONFIG)
        set_property(TARGET libplatform PROPERTY INTERFACE_LINK_LIBRARIES
            "${_libplatform_interface_links}")
    endif()
    unset(_libplatform_interface_links)
endif()

# libplatform-objects is compiled as a separate object library and therefore
# does not inherit the final library target's usage requirements.  Propagate
# the target-only headers explicitly for musl and POSIX support files.
foreach(_libplatform_compile_target IN ITEMS libplatform libplatform-objects)
    if(TARGET ${_libplatform_compile_target})
        if(USE_MUSL)
            target_compile_definitions(${_libplatform_compile_target} PRIVATE USE_MUSL=1)
        endif()
        if(DEFINED KERNEL_HEADERS_DIR AND EXISTS "${KERNEL_HEADERS_DIR}")
            target_include_directories(${_libplatform_compile_target} PRIVATE
                "${KERNEL_HEADERS_DIR}")
        endif()
        if(TARGET asciichat-libexecinfo)
            target_link_libraries(${_libplatform_compile_target} PRIVATE
                asciichat-libexecinfo)
        endif()
        if(TARGET asciichat-fontconfig)
            target_link_libraries(${_libplatform_compile_target} PRIVATE
                asciichat-fontconfig)
        endif()
    endif()
endforeach()
set(ASCIICHAT_LIBPLATFORM_TARGET libplatform::libplatform CACHE INTERNAL
    "Imported libplatform target used by ascii-chat")

unset(_ascii_chat_libplatform_source)
unset(_ascii_chat_libplatform_build)
unset(_ascii_chat_repo_root)
