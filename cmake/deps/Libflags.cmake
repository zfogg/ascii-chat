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
set(_asciichat_libflags_pcre2_include_dirs "${PCRE2_INCLUDE_DIRS}")
if(USE_MUSL)
    list(FILTER _asciichat_libflags_pcre2_include_dirs EXCLUDE REGEX "^/usr/include(/|$)")
endif()
set(LIBFLAGS_PCRE2_INCLUDE_DIRS "${_asciichat_libflags_pcre2_include_dirs}" CACHE STRING
    "PCRE2 headers shared with ascii-chat" FORCE)
if(USE_MUSL)
    set(LIBFLAGS_PCRE2_TARGET pcre2-8-static CACHE STRING
        "PCRE2 target shared with ascii-chat" FORCE)
    set(LIBFLAGS_PCRE2_INCLUDE_DIRS
        "${CMAKE_BINARY_DIR}/_deps/asciichat_pcre2/interface"
        CACHE STRING "PCRE2 headers shared with ascii-chat" FORCE)
    set(LIBFLAGS_PCRE2_LIBRARIES pcre2-8-static CACHE STRING
        "PCRE2 target shared with ascii-chat" FORCE)
endif()
set(LIBFLAGS_PCRE2_LIBRARIES "${PCRE2_LIBRARIES}" CACHE STRING
    "PCRE2 target shared with ascii-chat" FORCE)
set(LIBFLAGS_YYJSON_INCLUDE_DIRS "${YYJSON_INCLUDE_DIRS}" CACHE STRING
    "yyjson headers shared with ascii-chat" FORCE)
set(LIBFLAGS_YYJSON_LIBRARIES "${YYJSON_LIBRARIES}" CACHE STRING
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
set(_ascii_chat_libflags_source "${ASCIICHAT_DEPS_CACHE_ROOT}/libflags-src")
FetchContent_Declare(libflags
    GIT_REPOSITORY https://github.com/zfogg/libflags.git
    GIT_TAG main
    SOURCE_DIR "${_ascii_chat_libflags_source}")
set(_ascii_chat_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(libflags)
if(USE_MUSL AND TARGET flags)
    # libflags needs libplatform at link time, but its public dependency edge
    # also propagates libplatform's host pkg-config include roots (notably
    # /usr/include) into this musl target. Keep those dependencies link-only.
    get_target_property(_libflags_interface_links flags INTERFACE_LINK_LIBRARIES)
    if(_libflags_interface_links)
        string(REPLACE "libplatform::libplatform"
            "$<LINK_ONLY:libplatform::libplatform>"
            _libflags_interface_links "${_libflags_interface_links}")
        string(REPLACE "libflags-tomlc17"
            "$<LINK_ONLY:libflags-tomlc17>"
            _libflags_interface_links "${_libflags_interface_links}")
        set_property(TARGET flags PROPERTY INTERFACE_LINK_LIBRARIES
            "${_libflags_interface_links}")
    endif()
    unset(_libflags_interface_links)
endif()
if(TARGET libflags::flags)
    get_target_property(_libflags_real_target libflags::flags ALIASED_TARGET)
    if(_libflags_real_target)
        set_target_properties(${_libflags_real_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    else()
        set_target_properties(libflags::flags PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    unset(_libflags_real_target)
endif()
set(BUILD_SHARED_LIBS "${_ascii_chat_saved_build_shared_libs}")
unset(_ascii_chat_saved_build_shared_libs)
unset(_ascii_chat_libflags_source)
unset(_asciichat_libflags_pcre2_include_dirs)
