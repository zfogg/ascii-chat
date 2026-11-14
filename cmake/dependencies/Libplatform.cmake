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

# Use the checked-out submodule directly.  Reusing a cached source tree can
# mix headers and implementations from different libplatform revisions.
set(_ascii_chat_libplatform_source "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libplatform")
set(_ascii_chat_libplatform_build "${ASCIICHAT_DEPS_CACHE_DIR}/libplatform-build")
FetchContent_Declare(libplatform
    SOURCE_DIR "${_ascii_chat_libplatform_source}"
    BINARY_DIR "${_ascii_chat_libplatform_build}")
FetchContent_MakeAvailable(libplatform)

# libplatform is linked as a native shared library. Keep its dependency-only
# objects in the platform's normal object format: the application may enable
# ThinLTO for release builds, but embedding LLVM bitcode in this archive makes
# it unusable by the system linker used for the shared library.
foreach(_ascii_chat_no_lto_target IN ITEMS libplatform libplatform-objects ascii-chat-utf8proc utf8proc libplatform-utf8proc)
    if(TARGET ${_ascii_chat_no_lto_target})
        set_property(TARGET ${_ascii_chat_no_lto_target} PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
    endif()
endforeach()

# libplatform is an independently built shared library. Do not propagate the
# application's gcov instrumentation into its objects: Clang's profile runtime
# is linked into each instrumented executable, while lld rejects a shared
# library that leaves those gcov entry points unresolved. The application and
# its tests remain fully instrumented for coverage.
if(ASCIICHAT_ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    # Coverage flags are added to the parent project's interface target after
    # dependencies are configured. Defer the exclusion until the directory
    # finishes configuring so it is the final target-local option ordering.
    function(_ascii_chat_disable_libplatform_coverage)
        if(TARGET libplatform-objects)
            target_compile_options(libplatform-objects PRIVATE
                -fno-profile-arcs
                -fno-test-coverage)
        endif()
        foreach(_ascii_chat_utf8proc_target IN ITEMS utf8proc libplatform-utf8proc)
            if(TARGET ${_ascii_chat_utf8proc_target})
                target_compile_options(${_ascii_chat_utf8proc_target} PRIVATE
                    -fno-profile-arcs
                    -fno-test-coverage)
            endif()
        endforeach()
    endfunction()
    cmake_language(DEFER CALL _ascii_chat_disable_libplatform_coverage)

    # libplatform's private utf8proc archive is also a dependency-only target.
    # Keep it out of coverage instrumentation for the same reason: its objects
    # must not leave profile-runtime references in the archive consumed by the
    # platform library.
endif()

target_include_directories(asciichat-project-includes INTERFACE
    "${_ascii_chat_libplatform_source}/lib")
if(FONTCONFIG_LINK_LIBRARIES)
    target_link_libraries(libplatform PRIVATE ${FONTCONFIG_LINK_LIBRARIES})
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
unset(_ascii_chat_libplatform_build)
