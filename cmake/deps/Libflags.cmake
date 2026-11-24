# Configure the reusable typed command-line option library.
include(FetchContent)

set(LIBFLAGS_BUILD_TESTS OFF CACHE BOOL "Disable standalone libflags tests in ascii-chat" FORCE)
set(LIBFLAGS_DEPS_CACHE_ROOT "${ASCIICHAT_DEPS_CACHE_ROOT}" CACHE PATH
    "Shared dependency cache supplied by ascii-chat" FORCE)
set(LIBFLAGS_LIBPLATFORM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libplatform" CACHE PATH
    "libplatform source shared with ascii-chat" FORCE)
# Do not pass the host libc include root into a musl target.  libflags adds
# these paths as private compile includes, so an explicit /usr/include would
# otherwise reintroduce glibc headers after -nostdinc.
get_target_property(_asciichat_libflags_pcre2_include_dirs
    asciichat-pcre2 INTERFACE_INCLUDE_DIRECTORIES)
if(_asciichat_libflags_pcre2_include_dirs STREQUAL "_asciichat_libflags_pcre2_include_dirs-NOTFOUND")
    set(_asciichat_libflags_pcre2_include_dirs)
endif()
if(USE_MUSL)
    list(FILTER _asciichat_libflags_pcre2_include_dirs EXCLUDE REGEX "^/usr/include(/|$)")
endif()
set(LIBFLAGS_PCRE2_INCLUDE_DIRS "${_asciichat_libflags_pcre2_include_dirs}" CACHE STRING
    "PCRE2 headers shared with ascii-chat" FORCE)
if(USE_MUSL)
    set(LIBFLAGS_PCRE2_TARGET pcre2-8-static CACHE STRING
        "PCRE2 target shared with ascii-chat" FORCE)
    set(LIBFLAGS_PCRE2_INCLUDE_DIRS
        "${ASCIICHAT_DEPS_CACHE_DIR}/asciichat_pcre2/interface"
        CACHE STRING "PCRE2 headers shared with ascii-chat" FORCE)
    set(LIBFLAGS_PCRE2_LIBRARIES pcre2-8-static CACHE STRING
        "PCRE2 target shared with ascii-chat" FORCE)
endif()
set(LIBFLAGS_PCRE2_LIBRARIES asciichat-pcre2 CACHE STRING
    "PCRE2 target shared with ascii-chat" FORCE)
set(LIBFLAGS_YYJSON_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/yyjson/src" CACHE STRING
    "yyjson headers shared with ascii-chat" FORCE)
set(LIBFLAGS_YYJSON_LIBRARIES asciichat-yyjson CACHE STRING
    "yyjson target shared with ascii-chat" FORCE)
set(LIBFLAGS_TOMLC17_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/tomlc17" CACHE PATH
    "tomlc17 source shared with ascii-chat" FORCE)
# Reuse ascii-chat's already-configured utf8proc target. The libflags main
# branch supports this embedding point; do not create a second copy.
set(LIBFLAGS_UTF8PROC_TARGET utf8proc CACHE STRING
    "utf8proc target supplied by ascii-chat" FORCE)
set(LIBFLAGS_UTF8PROC_SOURCE_DIR "" CACHE PATH
    "Disable libflags' standalone utf8proc source when embedded" FORCE)
# libplatform is configured before libflags, so pass through its real utf8proc
# target instead of creating a second copy in the embedded dependency.
set(LIBPLATFORM_UTF8PROC_TARGET utf8proc CACHE STRING
    "utf8proc target supplied by ascii-chat" FORCE)
set(_asciichat_libflags_revision c51e165b CACHE INTERNAL
    "Pinned libflags revision" FORCE)
set(_ascii_chat_libflags_source "${ASCIICHAT_DEPS_CACHE_ROOT}/libflags-src-${CMAKE_BUILD_TYPE}")
if(EXISTS "${_ascii_chat_libflags_source}/CMakeLists.txt")
    execute_process(
        COMMAND git -C "${_ascii_chat_libflags_source}" rev-parse HEAD
        OUTPUT_VARIABLE _asciichat_cached_libflags_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _asciichat_cached_libflags_result)
    if(NOT _asciichat_cached_libflags_result EQUAL 0
            OR NOT _asciichat_cached_libflags_revision MATCHES "^${_asciichat_libflags_revision}($|[[:space:]])")
        message(STATUS
            "Rejecting stale libflags checkout at ${_ascii_chat_libflags_source}; "
            "restoring ${_asciichat_libflags_revision}.")
        file(REMOVE_RECURSE "${_ascii_chat_libflags_source}")
        set(_asciichat_cached_libflags_revision)
    endif()
endif()
if(EXISTS "${_ascii_chat_libflags_source}/CMakeLists.txt")
    asciichat_fetchcontent_declare(libflags SOURCE_DIR "${_ascii_chat_libflags_source}")
else()
    asciichat_fetchcontent_declare(libflags
        GIT_REPOSITORY https://github.com/zfogg/libflags.git
        GIT_TAG c51e165b
        SOURCE_DIR "${_ascii_chat_libflags_source}")
endif()
FetchContent_MakeAvailable(libflags)
if(TARGET libflags::libflags)
    set(ASCIICHAT_LIBFLAGS_TARGET libflags::libflags CACHE INTERNAL
        "libflags target selected by ascii-chat" FORCE)
elseif(TARGET libflags::flags)
    set(ASCIICHAT_LIBFLAGS_TARGET libflags::flags CACHE INTERNAL
        "libflags target selected by ascii-chat" FORCE)
endif()

if(NOT ASCIICHAT_LIBFLAGS_TARGET)
    message(FATAL_ERROR "libflags was configured without a usable CMake target")
endif()

get_target_property(_asciichat_libflags_real_target
    ${ASCIICHAT_LIBFLAGS_TARGET} ALIASED_TARGET)
if(NOT _asciichat_libflags_real_target)
    set(_asciichat_libflags_real_target ${ASCIICHAT_LIBFLAGS_TARGET})
endif()

if(USE_MUSL)
    # libflags needs libplatform at link time, but its public dependency edge
    # also propagates libplatform's host pkg-config include roots (notably
    # /usr/include) into this musl target. Keep those dependencies link-only.
    get_target_property(_libflags_interface_links
        ${_asciichat_libflags_real_target} INTERFACE_LINK_LIBRARIES)
    if(_libflags_interface_links)
        string(REPLACE "libplatform::libplatform"
            "$<LINK_ONLY:libplatform::libplatform>"
            _libflags_interface_links "${_libflags_interface_links}")
        string(REPLACE "libflags-tomlc17"
            "$<LINK_ONLY:libflags-tomlc17>"
            _libflags_interface_links "${_libflags_interface_links}")
        set_property(TARGET ${_asciichat_libflags_real_target}
            PROPERTY INTERFACE_LINK_LIBRARIES
            "${_libflags_interface_links}")
    endif()
endif()
set_target_properties(${_asciichat_libflags_real_target}
    PROPERTIES POSITION_INDEPENDENT_CODE ON)
unset(_libflags_interface_links)
unset(_asciichat_libflags_real_target)
unset(_ascii_chat_libflags_source)
unset(_asciichat_cached_libflags_revision)
unset(_asciichat_cached_libflags_result)
unset(_asciichat_libflags_revision)
unset(_asciichat_libflags_pcre2_include_dirs)
