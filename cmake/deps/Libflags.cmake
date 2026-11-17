# Configure the reusable typed command-line option library.
include(FetchContent)

set(LIBFLAGS_BUILD_TESTS OFF CACHE BOOL "Disable standalone libflags tests in ascii-chat" FORCE)
set(LIBFLAGS_DEPS_CACHE_ROOT "${ASCIICHAT_DEPS_CACHE_ROOT}" CACHE PATH
    "Shared dependency cache supplied by ascii-chat" FORCE)
set(LIBFLAGS_LIBPLATFORM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libplatform" CACHE PATH
    "libplatform source shared with ascii-chat" FORCE)
set(LIBFLAGS_PCRE2_INCLUDE_DIRS "${PCRE2_INCLUDE_DIRS}" CACHE STRING
    "PCRE2 headers shared with ascii-chat" FORCE)
set(LIBFLAGS_PCRE2_LIBRARIES "${PCRE2_LIBRARIES}" CACHE STRING
    "PCRE2 target shared with ascii-chat" FORCE)
set(LIBFLAGS_YYJSON_INCLUDE_DIRS "${YYJSON_INCLUDE_DIRS}" CACHE STRING
    "yyjson headers shared with ascii-chat" FORCE)
set(LIBFLAGS_YYJSON_LIBRARIES "${YYJSON_LIBRARIES}" CACHE STRING
    "yyjson target shared with ascii-chat" FORCE)
set(LIBFLAGS_TOMLC17_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/tomlc17" CACHE PATH
    "tomlc17 source shared with ascii-chat" FORCE)
set(LIBFLAGS_UTF8PROC_TARGET utf8proc CACHE STRING
    "utf8proc target shared with ascii-chat" FORCE)
# libplatform is configured before libflags, so pass through its real utf8proc
# target instead of creating a second copy in the embedded dependency.
set(LIBPLATFORM_UTF8PROC_TARGET utf8proc CACHE STRING
    "utf8proc target supplied by ascii-chat" FORCE)
set(_ascii_chat_libflags_source "${ASCIICHAT_DEPS_CACHE_ROOT}/libflags-src-${CMAKE_BUILD_TYPE}")
FetchContent_Declare(libflags
    GIT_REPOSITORY https://github.com/zfogg/libflags.git
    GIT_TAG 50c18f0
    SOURCE_DIR "${_ascii_chat_libflags_source}")
FetchContent_MakeAvailable(libflags)
unset(_ascii_chat_libflags_source)
