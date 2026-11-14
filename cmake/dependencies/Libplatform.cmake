# Configure the extracted libplatform dependency.
#
# Development builds use the shared library for fast iteration. Release-style
# builds use the static library unless the caller explicitly requests shared
# dependencies. musl is always static because its release toolchain is static.

include(FetchContent)

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
set(_ascii_chat_libplatform_source "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libplatform")
set(_ascii_chat_libplatform_build "${ASCIICHAT_DEPS_CACHE_DIR}/libplatform-build")
FetchContent_Declare(libplatform
    SOURCE_DIR "${_ascii_chat_libplatform_source}"
    BINARY_DIR "${_ascii_chat_libplatform_build}")
FetchContent_MakeAvailable(libplatform)

target_include_directories(asciichat-project-includes INTERFACE
    "${_ascii_chat_libplatform_source}/lib")

# libplatform owns the shared path/logging helpers, so give it the checkout
# root that CMake already knows instead of letting it fall back to ".".
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_ENABLE_COVERAGE)
    set(_ascii_chat_source_root ".")
else()
    string(REPLACE "\\" "/" _ascii_chat_source_root "${CMAKE_SOURCE_DIR}")
endif()
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
unset(_ascii_chat_libplatform_build)
unset(_ascii_chat_source_root)
