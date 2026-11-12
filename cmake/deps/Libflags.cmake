# Configure the reusable typed command-line option library.
include(FetchContent)

set(LIBFLAGS_BUILD_TESTS OFF CACHE BOOL "Disable standalone libflags tests in ascii-chat" FORCE)
set(_ascii_chat_libflags_source "${ASCIICHAT_DEPS_CACHE_ROOT}/libflags-src")
FetchContent_Declare(libflags
    GIT_REPOSITORY https://github.com/zfogg/libflags.git
    GIT_TAG 6ee0512
    SOURCE_DIR "${_ascii_chat_libflags_source}")
FetchContent_MakeAvailable(libflags)
unset(_ascii_chat_libflags_source)
