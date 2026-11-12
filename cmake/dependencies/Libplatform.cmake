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

set(_ascii_chat_libplatform_source "${ASCIICHAT_DEPS_CACHE_ROOT}/libplatform-src")
FetchContent_Declare(libplatform
    GIT_REPOSITORY https://github.com/zfogg/libplatform.git
    # Keep the cache clone on the mapped libplatform revision used by the
    # repository submodule. The copied compatibility files below are layered
    # onto this exact source, so headers and implementation stay in sync.
    GIT_TAG 6cf051cb5ba96a24452afe6e9fd45d0853d415bc
    SOURCE_DIR "${_ascii_chat_libplatform_source}")
FetchContent_MakeAvailable(libplatform)

target_include_directories(asciichat-project-includes INTERFACE
    "${_ascii_chat_libplatform_source}/lib")

# Apply the owned-directory source formatting contract to the active pinned
# source cache while preserving its dependency-specific include layout.
file(COPY "${CMAKE_SOURCE_DIR}/deps/libplatform/lib/symbols.c"
     DESTINATION "${_ascii_chat_libplatform_source}/lib")
file(COPY "${CMAKE_SOURCE_DIR}/deps/libplatform/lib/backtrace.c"
     DESTINATION "${_ascii_chat_libplatform_source}/lib")
file(COPY "${CMAKE_SOURCE_DIR}/deps/libplatform/lib/backtrace_internal.h"
     DESTINATION "${_ascii_chat_libplatform_source}/lib")
file(COPY "${CMAKE_SOURCE_DIR}/deps/libplatform/lib/debug/backtrace.c"
     DESTINATION "${_ascii_chat_libplatform_source}/lib/debug")
file(COPY "${CMAKE_SOURCE_DIR}/deps/libplatform/lib/debug/sync.c"
     DESTINATION "${_ascii_chat_libplatform_source}/lib/debug")
file(COPY "${CMAKE_SOURCE_DIR}/deps/libplatform/lib/debug/memory.c"
     DESTINATION "${_ascii_chat_libplatform_source}/lib/debug")
file(COPY "${CMAKE_SOURCE_DIR}/deps/libplatform/include/libplatform/symbols.h"
     DESTINATION "${_ascii_chat_libplatform_source}/include/libplatform")
file(COPY "${CMAKE_SOURCE_DIR}/deps/libplatform/include/libplatform/backtrace.h"
     DESTINATION "${_ascii_chat_libplatform_source}/include/libplatform")
file(COPY "${CMAKE_SOURCE_DIR}/deps/libplatform/include/libplatform/debug/backtrace.h"
     DESTINATION "${_ascii_chat_libplatform_source}/include/libplatform/debug")
file(COPY "${CMAKE_SOURCE_DIR}/deps/libplatform/lib/windows/backtrace.c"
     DESTINATION "${_ascii_chat_libplatform_source}/lib/windows")

# libplatform owns the shared path/logging helpers, so give it the checkout
# root that CMake already knows instead of letting it fall back to ".".
string(REPLACE "\\" "/" _ascii_chat_source_root "${CMAKE_SOURCE_DIR}")
target_compile_definitions(libplatform-objects PRIVATE
    LIBPLATFORM_SOURCE_ROOT="${_ascii_chat_source_root}")

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
unset(_ascii_chat_source_root)
