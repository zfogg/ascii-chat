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

if(TARGET asciichat-project-includes)
    target_include_directories(asciichat-project-includes INTERFACE
        "${_ascii_chat_libplatform_source}/lib")
endif()

# libplatform's current POSIX implementation uses the application's public
# error and utility contracts. Keep that mapping private to libplatform so its
# headers and symbols are never propagated to consumers.
foreach(_libplatform_target IN ITEMS libplatform libplatform-objects)
    if(TARGET ${_libplatform_target})
        target_include_directories(${_libplatform_target} PRIVATE
            "${_ascii_chat_libplatform_source}/lib"
            "${_ascii_chat_repo_root}/include"
            "${_ascii_chat_repo_root}/deps"
            "${_ascii_chat_repo_root}/deps/ascii-chat-deps/yyjson/src"
            "${_ascii_chat_repo_root}/deps/ascii-chat-deps/utf8proc"
            "${_ascii_chat_repo_root}/deps/ascii-chat-deps/gotel/c/include"
            "${CMAKE_BINARY_DIR}/_deps/gotel/otlp_c-src/include"
            "${CMAKE_BINARY_DIR}/_deps/gotel/sequel-src/include"
            ${SQLITE3_INCLUDE_DIRS})
        target_compile_definitions(${_libplatform_target} PRIVATE GOTEL_WITH_SQLITE=1)
    endif()
endforeach()

# libplatform owns the shared path/logging helpers, so give it the checkout
# root that CMake already knows instead of letting it fall back to ".".
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_ENABLE_COVERAGE)
    set(_ascii_chat_source_root ".")
else()
    string(REPLACE "\\" "/" _ascii_chat_source_root "${CMAKE_SOURCE_DIR}")
endif()
target_compile_definitions(libplatform PRIVATE
    LIBPLATFORM_SOURCE_ROOT="${_ascii_chat_source_root}")

# Coverage is an application-level instrumentation option.  Do not propagate
# its profile runtime into the shared libplatform DSO: executables own the
# coverage runtime and export its symbols, while DSOs must remain
# uninstrumented or lld rejects their unresolved gcov references.
if(ASCIICHAT_ENABLE_COVERAGE)
    foreach(_libplatform_coverage_target IN ITEMS libplatform libplatform-objects)
        if(TARGET ${_libplatform_coverage_target})
            target_compile_options(${_libplatform_coverage_target} PRIVATE
                -fno-profile-instr-generate
                -fno-profile-arcs
                -fno-test-coverage)
        endif()
    endforeach()
    if(TARGET libplatform)
        target_link_options(libplatform PRIVATE
            -fno-profile-instr-generate
            -fno-profile-arcs
            -fno-test-coverage)
    endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
    target_compile_definitions(libplatform PRIVATE DEBUG_MEMORY)
endif()
if(LIBSODIUM_INCLUDE_DIRS)
    foreach(_libplatform_target IN ITEMS libplatform libplatform-objects)
        if(TARGET ${_libplatform_target})
            target_include_directories(${_libplatform_target} PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
        endif()
    endforeach()
endif()
if(EMSCRIPTEN)
    foreach(_libplatform_target IN ITEMS libplatform libplatform-objects)
        if(TARGET ${_libplatform_target})
            target_compile_options(${_libplatform_target} PRIVATE -pthread -matomics -mbulk-memory)
            # The current libplatform WASM compatibility layer still uses a
            # small set of ascii-chat error and utility declarations. Keep
            # those headers private to this transitional WASM build boundary;
            # native libplatform targets remain independent of ascii-chat.
            target_include_directories(${_libplatform_target} PRIVATE
                "${_ascii_chat_repo_root}/include"
                "${_ascii_chat_repo_root}/deps"
                "${_ascii_chat_repo_root}/deps/ascii-chat-deps/yyjson/src"
                "${_ascii_chat_repo_root}/deps/ascii-chat-deps/utf8proc")
        endif()
    endforeach()
endif()


# libplatform's POSIX helpers use interfaces such as fileno() that musl hides
# from strict C mode unless the feature-test macros are enabled on the target.
if(UNIX AND NOT APPLE)
    target_compile_definitions(libplatform PRIVATE _GNU_SOURCE)
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
