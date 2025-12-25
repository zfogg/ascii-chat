# =============================================================================
# Source Files Module
# =============================================================================
# This module collects all source files for each library module
#
# Prerequisites:
#   - Platform detection complete (WIN32, PLATFORM_DARWIN, etc.)
#   - USE_MUSL known
#   - SIMD detection complete (ENABLE_SIMD_* variables)
#
# Outputs:
#   - UTIL_SRCS
#   - CRYPTO_SRCS
#   - PLATFORM_SRCS
#   - SIMD_SRCS
#   - VIDEO_SRCS
#   - AUDIO_SRCS
#   - NETWORK_SRCS
#   - CORE_SRCS
#   - DATA_STRUCTURES_SRCS
# =============================================================================

# =============================================================================
# Module 1: Utilities (ultra-stable - changes rarely)
# =============================================================================
set(UTIL_SRCS
    lib/util/format.c
    lib/util/parsing.c
    lib/util/path.c
    lib/util/string.c
    lib/util/math.c
    lib/util/ip.c
    lib/util/aspect_ratio.c
    lib/util/time.c
    lib/util/levenshtein.c
)

# Add C23 compatibility wrappers for musl (provides __isoc23_* symbols)
if(USE_MUSL)
    list(APPEND UTIL_SRCS lib/musl_c23_compat.c)
endif()

# =============================================================================
# Module 2: Cryptography (stable - changes monthly)
# =============================================================================
set(CRYPTO_SRCS
    lib/crypto/crypto.c
    lib/crypto/keys/keys.c
    lib/crypto/known_hosts.c
    lib/crypto/handshake.c
    lib/crypto/http_client.c
    lib/crypto/pem_utils.c
    # lib/crypto/gpg.c  # Temporarily excluded
    lib/crypto/ssh_agent.c
    lib/crypto/keys/ssh_keys.c
    # lib/crypto/keys/gpg_keys.c  # Temporarily excluded
    lib/crypto/keys/gpg_stubs.c  # Stub implementations for disabled GPG support
    lib/crypto/keys/https_keys.c
    lib/crypto/keys/validation.c
    # libsodium-bcrypt-pbkdf (OpenBSD implementation)
    deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
)

# Suppress specific Clang warnings for libsodium-bcrypt-pbkdf (third-party code)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(
        deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
        deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
        PROPERTIES
        COMPILE_FLAGS "-Wno-sizeof-array-div"
    )
endif()

# Disable static analyzers for third-party libsodium-bcrypt-pbkdf code
set_source_files_properties(
    deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
    PROPERTIES
    SKIP_LINTING ON
)

# =============================================================================
# Module 3: Platform Abstraction (stable - changes monthly)
# =============================================================================
set(PLATFORM_SRCS_COMMON
    lib/platform/abstraction.c
    # NOTE: lib/platform/system.c is included by windows/system.c and posix/system.c
)

if(WIN32)
    set(PLATFORM_SRCS
        ${PLATFORM_SRCS_COMMON}
        lib/platform/windows/thread.c
        lib/platform/windows/mutex.c
        lib/platform/windows/rwlock.c
        lib/platform/windows/cond.c
        lib/platform/windows/terminal.c
        lib/platform/windows/system.c
        lib/platform/windows/socket.c
        lib/platform/windows/string.c
        lib/platform/windows/password.c
        lib/platform/windows/symbols.c
        lib/platform/windows/getopt.c
        lib/platform/windows/pipe.c
        lib/os/windows/webcam_mediafoundation.c
    )
elseif(PLATFORM_POSIX)
    # POSIX platforms (Linux/macOS)
    set(PLATFORM_SRCS
        ${PLATFORM_SRCS_COMMON}
        lib/platform/posix/thread.c
        lib/platform/posix/mutex.c
        lib/platform/posix/rwlock.c
        lib/platform/posix/cond.c
        lib/platform/posix/terminal.c
        lib/platform/posix/system.c
        lib/platform/posix/socket.c
        lib/platform/posix/string.c
        lib/platform/posix/password.c
        lib/platform/posix/symbols.c
        lib/platform/posix/pipe.c
    )

    if(PLATFORM_DARWIN)
        list(APPEND PLATFORM_SRCS
            lib/os/macos/webcam_avfoundation.m
        )
    elseif(PLATFORM_LINUX)
        list(APPEND PLATFORM_SRCS
            lib/os/linux/webcam_v4l2.c
        )
    else()
        message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}. We don't have webcam code for this platform.")
    endif()
endif()

# =============================================================================
# Module 4: SIMD (performance-critical - changes weekly)
# =============================================================================
set(SIMD_SRCS)

# Always include common SIMD files
list(APPEND SIMD_SRCS
    lib/image2ascii/simd/ascii_simd.c
    lib/image2ascii/simd/ascii_simd_color.c
    lib/image2ascii/simd/common.c
    lib/image2ascii/output_buffer.c
    lib/image2ascii/rle.c
)

# Architecture-specific SIMD sources based on detection
if(ENABLE_SIMD_SSE2)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/sse2.c)
endif()

if(ENABLE_SIMD_SSSE3)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/ssse3.c)
endif()

if(ENABLE_SIMD_AVX2)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/avx2.c)
    set_source_files_properties(lib/image2ascii/simd/avx2.c PROPERTIES COMPILE_FLAGS "-mavx2")
endif()

if(ENABLE_SIMD_NEON)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/neon.c)
endif()

if(ENABLE_SIMD_SVE)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/sve.c)
    set_source_files_properties(lib/image2ascii/simd/sve.c PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve")
endif()

# =============================================================================
# Module 5: Video Processing (changes weekly)
# =============================================================================
set(VIDEO_SRCS
    lib/video_frame.c
    lib/image2ascii/image.c
    lib/image2ascii/ascii.c
    lib/image2ascii/ansi_fast.c
    lib/image2ascii/ansi.c
    lib/util/utf8.c
    lib/os/webcam.c
)

# Platform-specific webcam sources already added to PLATFORM_SRCS
# (webcam_avfoundation.m, webcam_v4l2.c, webcam_mediafoundation.c)

# =============================================================================
# Module 6: Audio Processing (changes weekly)
# =============================================================================
set(AUDIO_SRCS
    lib/audio/audio.c
    lib/audio/mixer.c
    lib/audio/wav_writer.c
    lib/audio/opus_codec.c
    lib/audio/audio_analysis.c
    lib/audio/client_audio_pipeline.cpp
)

# CRITICAL: client_audio_pipeline.cpp includes WebRTC headers which require C++17.
# The main ascii-chat project uses C++26, so we must override the C++ standard
# for just this file to avoid Abseil's std::result_of errors in C++26.
set_source_files_properties(
    lib/audio/client_audio_pipeline.cpp
    PROPERTIES
    COMPILE_FLAGS "-std=c++17"
    LANGUAGE CXX
)

# =============================================================================
# Module 7: Network (changes weekly)
# =============================================================================
set(NETWORK_SRCS
    lib/network/network.c
    lib/network/packet.c
    lib/network/av.c
    lib/compression.c
    lib/crc32.c
)

# =============================================================================
# Module 8: Core Application (changes daily)
# =============================================================================
set(CORE_SRCS
    lib/common.c
    lib/asciichat_errno.c
    lib/logging.c
    lib/options.c
    lib/config.c
    lib/version.c
    lib/palette.c
    # Add tomlc17 parser source
    ${CMAKE_SOURCE_DIR}/deps/tomlc17/src/tomlc17.c
)

# Only include lock debugging runtime in non-release builds (when NDEBUG is not defined)
# debug/lock.c is wrapped in #ifndef NDEBUG, so it's safe to compile in release,
# but we exclude it for clarity and to avoid unnecessary compilation
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    list(APPEND CORE_SRCS lib/debug/lock.c)
    list(APPEND CORE_SRCS lib/debug/memory.c)
endif()

# Disable precompiled headers and static analyzers for tomlc17 (third-party code)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/deps/tomlc17/src/tomlc17.c
    PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    SKIP_LINTING ON
)

# =============================================================================
# Data Structures Module
# =============================================================================
set(DATA_STRUCTURES_SRCS
    lib/ringbuffer.c
    lib/packet_queue.c
    lib/buffer_pool.c
)

# =============================================================================
# Panic Instrumentation Runtime
# =============================================================================
set(TOOLING_PANIC_SRCS
    lib/tooling/panic/instrument_log.c
    lib/tooling/panic/instrument_cov.c
)

set(TOOLING_PANIC_REPORT_SRCS
    src/tooling/panic/report.c
)

# =============================================================================
# Application Sources (main executable)
# =============================================================================
set(APP_SRCS
    src/main.c
    # Server mode sources
    src/server/main.c
    src/server/client.c
    src/server/protocol.c
    src/server/crypto.c
    src/server/stream.c
    src/server/render.c
    src/server/stats.c
    # Client mode sources
    src/client/main.c
    src/client/mirror.c
    src/client/server.c
    src/client/protocol.c
    src/client/crypto.c
    src/client/display.c
    src/client/capture.c
    src/client/audio.c
    src/client/keepalive.c
)

