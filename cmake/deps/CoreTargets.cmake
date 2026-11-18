include_guard(GLOBAL)
include(${CMAKE_SOURCE_DIR}/cmake/utils/DependencyTargets.cmake)

add_library(asciichat-core-dependencies INTERFACE)

function(_asciichat_wrap_target canonical)
    if(TARGET "${canonical}")
        return()
    endif()
    foreach(_candidate IN LISTS ARGN)
        if(TARGET "${_candidate}")
            add_library("${canonical}" INTERFACE)
            target_link_libraries("${canonical}" INTERFACE "${_candidate}")
            return()
        endif()
    endforeach()
    # Package modules that have not yet migrated may still expose an absolute
    # archive path.  Convert that legacy result into the same interface-target
    # contract here; consumers never link the variable directly.
    set(_archives)
    foreach(_candidate IN LISTS ARGN)
        if(IS_ABSOLUTE "${_candidate}" OR EXISTS "${_candidate}")
            list(APPEND _archives "${_candidate}")
        endif()
    endforeach()
    if(_archives)
        add_library("${canonical}" INTERFACE)
        target_link_libraries("${canonical}" INTERFACE ${_archives})
        return()
    endif()
    message(FATAL_ERROR "Required dependency target is missing: ${canonical}")
endfunction()

_asciichat_wrap_target(asciichat-portaudio portaudio portaudio_static)
_asciichat_wrap_target(asciichat-opus Opus::opus opus)
_asciichat_wrap_target(asciichat-fftw FFTW3::fftw3 fftw3 fftw3f)
_asciichat_wrap_target(asciichat-fftw-float FFTW3::fftw3f fftw3f)
_asciichat_wrap_target(asciichat-zstd zstd::libzstd zstd::zstd)
_asciichat_wrap_target(asciichat-libsodium asciichat-libsodium unofficial-sodium::sodium sodium)

foreach(_target IN ITEMS
    asciichat-libuv asciichat-alsa asciichat-portaudio asciichat-opus asciichat-fftw asciichat-fftw-float
    asciichat-zstd asciichat-libsodium asciichat-rnnoise)
    target_link_libraries(asciichat-core-dependencies INTERFACE "${_target}")
endforeach()

# Canonical dependency contract.  Each dependency module may use a native
# imported target, a source-built target, or a package target internally; the
# rest of ascii-chat consumes only these stable names.
_asciichat_wrap_target(asciichat-libplatform libplatform::libplatform)
_asciichat_wrap_target(asciichat-libflags libflags::flags)
if(NOT EMSCRIPTEN)
    _asciichat_wrap_target(asciichat-libuv)
endif()
_asciichat_wrap_target(asciichat-utf8proc utf8proc)
_asciichat_wrap_target(asciichat-yyjson yyjson::yyjson yyjson)
_asciichat_wrap_target(asciichat-pcre2)
_asciichat_wrap_target(asciichat-sqlite3)
_asciichat_wrap_target(asciichat-libwebsockets libwebsockets::libwebsockets websockets websockets_shared)
_asciichat_wrap_target(asciichat-libdatachannel libdatachannel)
_asciichat_wrap_target(asciichat-miniupnpc)
_asciichat_wrap_target(asciichat-freetype Freetype::Freetype PkgConfig::FREETYPE freetype)
if(NOT WIN32)
    _asciichat_wrap_target(asciichat-fontconfig PkgConfig::FONTCONFIG ${FONTCONFIG_LINK_LIBRARIES})
endif()
_asciichat_wrap_target(asciichat-vterm asciichat-vterm PkgConfig::VTERM vterm)
_asciichat_wrap_target(asciichat-openssl OpenSSL::SSL OpenSSL::Crypto)
_asciichat_wrap_target(asciichat-bearssl)
if(PLATFORM_LINUX)
    _asciichat_wrap_target(asciichat-systemd asciichat-systemd)
endif()
_asciichat_wrap_target(asciichat-webrtc-aec3 webrtc_audio_processing)
_asciichat_wrap_target(asciichat-ffmpeg)
_asciichat_wrap_target(asciichat-x264 x264::x264)
_asciichat_wrap_target(asciichat-x265 x265::x265)
_asciichat_wrap_target(asciichat-bzip2 BZip2::BZip2)
_asciichat_wrap_target(asciichat-expat EXPAT::EXPAT expat)
_asciichat_wrap_target(asciichat-fftw asciichat-fftw)
_asciichat_wrap_target(asciichat-fftw-float asciichat-fftw-float)
_asciichat_wrap_target(asciichat-gotel gotel::gotel gotel)
_asciichat_wrap_target(asciichat-ngtcp2 asciichat-ngtcp2 ngtcp2_static ngtcp2)
_asciichat_wrap_target(asciichat-nghttp3 asciichat-nghttp3 nghttp3)
_asciichat_wrap_target(asciichat-quic asciichat-quic)
_asciichat_wrap_target(asciichat-zlib asciichat-zlib ZLIB::ZLIB ZLIB::zlib)
_asciichat_wrap_target(asciichat-zstd asciichat-zstd zstd::libzstd)
_asciichat_wrap_target(asciichat-libsodium asciichat-libsodium)
_asciichat_wrap_target(asciichat-opus asciichat-opus)
if(NOT PLATFORM_IOS AND NOT EMSCRIPTEN)
    _asciichat_wrap_target(asciichat-portaudio asciichat-portaudio)
endif()
_asciichat_wrap_target(asciichat-rnnoise asciichat-rnnoise)

if(NOT TARGET asciichat-abseil)
    add_library(asciichat-abseil INTERFACE)
    foreach(_absl_target IN ITEMS absl::base absl::strings absl::synchronization)
        if(TARGET "${_absl_target}")
            target_link_libraries(asciichat-abseil INTERFACE "${_absl_target}")
        endif()
    endforeach()
endif()

if(NOT TARGET asciichat-openssl)
    add_library(asciichat-openssl INTERFACE)
    target_link_libraries(asciichat-openssl INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()

# The aggregate is also the transitive closure for the executable.  Keep these
# module-owned targets here so static consumers cannot accidentally drop a
# dependency merely because its old *_LIBRARIES variable was empty.
foreach(_target IN ITEMS
    asciichat-icu asciichat-ctoon asciichat-ffmpeg asciichat-quic
    asciichat-ngtcp2 ngtcp2 ngtcp2_static ngtcp2_crypto_ossl_static
    asciichat-nghttp3 nghttp3 asciichat-gotel
    gotel::gotel gotel asciichat-gotel-pcre2 asciichat-gotel-sqlite
    asciichat-utf8proc utf8proc libplatform-utf8proc
    asciichat-libflags libflags::flags
    asciichat-nghttp2 nghttp2::nghttp2 nghttp2 nghttp2_static asciichat-vcpkg-nghttp2
    asciichat-webrtc-aec3 webrtc_audio_processing
    asciichat-vterm asciichat-miniupnpc
    asciichat-libdatachannel asciichat-libwebsockets asciichat-freetype
    asciichat-fontconfig asciichat-openssl asciichat-bearssl
    asciichat-sqlite3 asciichat-pcre2 asciichat-libsodium asciichat-zstd
    asciichat-portaudio asciichat-opus asciichat-ffmpeg asciichat-mimalloc)
    if(TARGET "${_target}")
        target_link_libraries(asciichat-core-dependencies INTERFACE "${_target}")
    endif()
endforeach()

if(TARGET asciichat-libexecinfo)
    target_link_libraries(asciichat-core-dependencies INTERFACE asciichat-libexecinfo)
endif()
