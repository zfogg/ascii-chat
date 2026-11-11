# Configure the extracted libplatform dependency.
#
# Development builds use the shared library for fast iteration. Release-style
# builds use the static library unless the caller explicitly requests shared
# dependencies. musl is always static because its release toolchain is static.

include(FetchContent)

if(USE_MUSL)
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

get_filename_component(_ascii_chat_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_subdirectory(${_ascii_chat_repo_root}/deps/libplatform
                 ${CMAKE_BINARY_DIR}/deps/libplatform)
if(EMSCRIPTEN)
    get_target_property(_libplatform_sources libplatform-objects SOURCES)
    foreach(_generic_source
        lib/symbols.c
        lib/filesystem_common.c
        lib/backtrace.c
        lib/keyboard.c
        lib/socket_io.c
        lib/cond.c
        lib/terminal.c
        lib/lifecycle.c
        lib/mutex.c
        lib/system_common.c
        lib/util/string.c

        lib/wasm/stubs/audio.c
        lib/wasm/stubs/backtrace.c
        lib/wasm/backtrace.c
        lib/wasm/stubs/comprehensive.c
        lib/wasm/stubs/misc.c)
        list(REMOVE_ITEM _libplatform_sources
            ${_generic_source}
            ${_ascii_chat_repo_root}/deps/libplatform/${_generic_source})
    endforeach()
    set_property(TARGET libplatform-objects PROPERTY SOURCES ${_libplatform_sources})
    unset(_libplatform_sources)
endif()
target_include_directories(libplatform-objects PRIVATE
    ${_ascii_chat_repo_root}/include
    ${_ascii_chat_repo_root}/deps
    ${_ascii_chat_repo_root}/deps/ascii-chat-deps/yyjson/src
    ${_ascii_chat_repo_root}/deps/ascii-chat-deps/utf8proc)
if(LIBSODIUM_INCLUDE_DIRS)
    target_include_directories(libplatform-objects PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
endif()
if(EMSCRIPTEN)
    target_compile_options(libplatform-objects PRIVATE -pthread -matomics -mbulk-memory)
endif()

# libplatform's POSIX helpers use interfaces such as fileno() that musl hides
# from strict C mode unless the feature-test macros are enabled on the target.
if(UNIX AND NOT APPLE)
    target_compile_definitions(libplatform-objects PRIVATE _GNU_SOURCE)
    if(DEFINED KERNEL_HEADERS_DIR AND EXISTS "${KERNEL_HEADERS_DIR}")
        target_include_directories(libplatform-objects PRIVATE "${KERNEL_HEADERS_DIR}")
    endif()
    if(DEFINED FONTCONFIG_INCLUDE_DIRS)
        target_include_directories(libplatform-objects PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
    endif()
    if(DEFINED LIBEXECINFO_INCLUDE_DIRS)
        target_include_directories(libplatform-objects PRIVATE ${LIBEXECINFO_INCLUDE_DIRS})
    endif()
endif()

unset(_ascii_chat_libplatform_shared)
unset(_ascii_chat_libplatform_source)
unset(_ascii_chat_repo_root)
