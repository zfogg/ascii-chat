cmake_minimum_required(VERSION 3.30)
project(ascii-chat-source-dependencies LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(ASCIICHAT_BUILD_DEPS_FROM_SOURCE ON)
set(USE_MIMALLOC ON)
option(BUILD_SHARED_LIBS "Exercise shared dependency targets" OFF)
get_filename_component(ASCIICHAT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT ASCIICHAT_DEPS_CACHE_DIR)
    set(ASCIICHAT_DEPS_CACHE_DIR "${ASCIICHAT_ROOT}/.deps-cache/source-dependencies" CACHE PATH
        "ascii-chat dependency cache for the standalone dependency fixture")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(ASCIICHAT_TARGET_ARM64 TRUE)
    set(ENABLE_SIMD_NEON ON)
endif()
foreach(_dependency Zlib Zstd PCRE2 Opus Portaudio FreeType2 Abseil Miniupnpc Expat Libvterm Utf8proc)
    include("${ASCIICHAT_ROOT}/cmake/deps/${_dependency}.cmake")
endforeach()
find_package(OpenSSL REQUIRED)
foreach(_dependency Libwebsockets Libdatachannel WebRTC)
    include("${ASCIICHAT_ROOT}/cmake/deps/${_dependency}.cmake")
endforeach()
include("${ASCIICHAT_ROOT}/cmake/deps/Mimalloc.cmake")
add_executable(source-dependencies main.cpp)
# Use the selected allocator headers even when a TLS package shares its include prefix.
target_link_libraries(source-dependencies PRIVATE
    asciichat-zlib asciichat-zstd asciichat-pcre2 asciichat-opus
    asciichat-portaudio asciichat-freetype asciichat-abseil asciichat-miniupnpc
    asciichat-expat asciichat-vterm asciichat-libwebsockets
    asciichat-libdatachannel asciichat-webrtc-aec3 asciichat-mimalloc
    asciichat-utf8proc)
enable_testing()
add_test(NAME source-dependencies COMMAND source-dependencies)
