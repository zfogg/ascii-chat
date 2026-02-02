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
    lib/util/url.c
    lib/util/string.c
    lib/util/ip.c
    lib/util/aspect_ratio.c
    lib/util/time.c
    lib/util/image.c
    lib/util/password.c
    lib/util/fps.c
    lib/util/utf8.c
    # utf8proc Unicode library (includes utf8proc_data.c internally)
    deps/ascii-chat-deps/utf8proc/utf8proc.c
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
    lib/crypto/keys.c
    lib/crypto/known_hosts.c
    lib/crypto/handshake/common.c
    lib/crypto/handshake/server.c
    lib/crypto/handshake/client.c
    lib/crypto/pem_utils.c
    # GPG module (refactored into gpg/ subdirectory)
    lib/crypto/gpg/agent.c
    lib/crypto/gpg/export.c
    lib/crypto/gpg/signing.c
    lib/crypto/gpg/verification.c
    lib/crypto/gpg/gpg_keys.c  # GPG key parsing
    lib/crypto/gpg/openpgp.c   # OpenPGP packet format parser (RFC 4880)
    lib/crypto/gpg/homedir.c   # Temporary GPG homedir management
    # SSH module (refactored into ssh/ subdirectory)
    lib/crypto/ssh/ssh_agent.c
    lib/crypto/ssh/ssh_keys.c  # SSH key parsing
    # Key management (at crypto root)
    lib/crypto/https_keys.c    # GitHub/GitLab key fetching
    lib/crypto/discovery_keys.c     # ACDS server key trust management
    lib/crypto/keys_validation.c  # Key validation utilities
    # libsodium-bcrypt-pbkdf (OpenBSD implementation)
    deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
)

# Suppress specific Clang warnings for libsodium-bcrypt-pbkdf (third-party code)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(
        deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
        deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
        PROPERTIES
        COMPILE_FLAGS "-Wno-sizeof-array-div"
    )
endif()

# Disable static analyzers for third-party libsodium-bcrypt-pbkdf code
set_source_files_properties(
    deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
    PROPERTIES
    SKIP_LINTING ON
)

# Suppress warnings for third-party mdns library (we don't control this code)
# Can't use -w because it disables -Wwrite-strings which breaks PCH compatibility
set_source_files_properties(
    deps/ascii-chat-deps/mdns/mdns.c
    PROPERTIES
    COMPILE_FLAGS "-Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -Wno-unused-but-set-variable"
    SKIP_LINTING ON
)

# =============================================================================
# Module 3: Platform Abstraction (stable - changes monthly)
# =============================================================================
set(PLATFORM_SRCS_COMMON
    lib/platform/abstraction.c
    lib/platform/socket.c
    lib/platform/thread.c
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
        lib/platform/windows/keyboard.c
        lib/platform/windows/system.c
        lib/platform/windows/keepawake.c
        lib/platform/windows/socket.c
        lib/platform/windows/string.c
        lib/platform/windows/util.c
        lib/platform/windows/question.c
        lib/platform/windows/mmap.c
        lib/platform/windows/symbols.c
        lib/platform/windows/getopt.c
        lib/platform/windows/pipe.c
        lib/platform/windows/memory.c
        lib/platform/windows/process.c
        lib/platform/windows/filesystem.c
        lib/platform/windows/errno.c
        lib/platform/windows/agent.c
        lib/video/webcam/windows/webcam_mediafoundation.c
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
        lib/platform/posix/keyboard.c
        lib/platform/posix/system.c
        lib/platform/posix/socket.c
        lib/platform/posix/string.c
        lib/platform/posix/util.c
        lib/platform/posix/question.c
        lib/platform/posix/mmap.c
        lib/platform/posix/symbols.c
        lib/platform/posix/pipe.c
        lib/platform/posix/memory.c
        lib/platform/posix/process.c
        lib/platform/posix/filesystem.c
        lib/platform/posix/errno.c
        lib/platform/posix/agent.c
    )

    if(PLATFORM_DARWIN)
        list(APPEND PLATFORM_SRCS
            lib/platform/macos/keepawake.c
            lib/video/webcam/macos/webcam_avfoundation.m
        )
    elseif(PLATFORM_LINUX)
        list(APPEND PLATFORM_SRCS
            lib/platform/linux/keepawake.c
            lib/video/webcam/linux/webcam_v4l2.c
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
    lib/video/simd/ascii_simd.c
    lib/video/simd/ascii_simd_color.c
    lib/video/simd/common.c
    lib/video/output_buffer.c
    lib/video/rle.c
)

# Architecture-specific SIMD sources based on detection
if(ENABLE_SIMD_SSE2)
    list(APPEND SIMD_SRCS lib/video/simd/sse2.c)
endif()

if(ENABLE_SIMD_SSSE3)
    list(APPEND SIMD_SRCS lib/video/simd/ssse3.c)
endif()

if(ENABLE_SIMD_AVX2)
    list(APPEND SIMD_SRCS lib/video/simd/avx2.c)
    set_source_files_properties(lib/video/simd/avx2.c PROPERTIES COMPILE_FLAGS "-mavx2")
endif()

if(ENABLE_SIMD_NEON)
    list(APPEND SIMD_SRCS lib/video/simd/neon.c)
endif()

if(ENABLE_SIMD_SVE)
    list(APPEND SIMD_SRCS lib/video/simd/sve.c)
    set_source_files_properties(lib/video/simd/sve.c PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve")
endif()

# =============================================================================
# Module 5: Video Processing (changes weekly)
# =============================================================================
set(VIDEO_SRCS
    lib/video/video_frame.c
    lib/video/image.c
    lib/video/ascii.c
    lib/video/ansi_fast.c
    lib/video/ansi.c
    lib/video/palette.c
    lib/video/color_filter.c
    lib/video/webcam/webcam.c
)

# Media source files (only when FFmpeg is available)
if(FFMPEG_FOUND)
    list(APPEND VIDEO_SRCS
        lib/media/source.c
        lib/media/ffmpeg_decoder.c
        lib/media/youtube.c
    )
endif()

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
    lib/audio/analysis.c
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
    lib/network/packet_parsing.c
    lib/network/frame_validator.c
    lib/network/compression.c
    lib/network/crc32.c
    lib/network/packet_queue.c
    lib/network/http_client.c
    lib/network/errors.c
    lib/network/parallel_connect.c
    # Rate limiting library (backend abstraction)
    lib/network/rate_limit/rate_limit.c
    lib/network/rate_limit/memory.c
    lib/network/rate_limit/sqlite.c
    # TCP transport layer
    lib/network/tcp/server.c
    lib/network/tcp/client.c
    lib/network/tcp/transport.c
    # WebSocket transport layer
    lib/network/websocket/transport.c
    # WebRTC P2P transport
    lib/network/webrtc/webrtc.c
    lib/network/webrtc/transport.c
    lib/network/webrtc/peer_manager.c
    lib/network/webrtc/stun.c
    lib/network/webrtc/turn_credentials.c
    lib/network/webrtc/sdp.c
    lib/network/webrtc/ice.c
    lib/network/webrtc/ice_selected_pair.cpp
    # ACIP protocol library (transport-agnostic)
    lib/network/acip/client.c
    lib/network/acip/server.c
    lib/network/acip/acds_client.c
    lib/network/acip/acds_server.c
    lib/network/acip/send.c
    lib/network/acip/handlers.c
    lib/network/acip/acds_handlers.c
    # NAT traversal (UPnP/NAT-PMP for direct TCP without WebRTC)
    lib/network/nat/upnp.c
    # mDNS service discovery for LAN
    lib/network/mdns/mdns.c
    lib/network/mdns/discovery_tui.c
    lib/network/mdns/discovery.c
    deps/ascii-chat-deps/mdns/mdns.c
)

# ice_selected_pair.cpp: Uses standard C++26 like the rest of the project
# The file includes libdatachannel's C++ headers (<rtc/rtc.hpp>) which use <atomic>,
# and other headers transitively include <stdatomic.h>. These are incompatible before C++23,
# but C++26 (our project standard) allows both to coexist without conflict.
set_source_files_properties(
    lib/network/webrtc/ice_selected_pair.cpp
    PROPERTIES
    LANGUAGE CXX
)

# =============================================================================
# Module 8: Core Application (changes daily)
# =============================================================================
set(CORE_SRCS
    lib/common.c
    lib/asciichat_errno.c
    lib/embedded_resources.c    # Resource embedding for production builds
    lib/log/logging.c
    lib/log/colorize.c
    lib/log/mmap.c
    lib/platform/terminal.c     # Unified color detection system
    lib/options/colorscheme.c   # Color scheme management and early initialization
    lib/options/options.c
    lib/options/common.c
    lib/options/validation.c
    lib/options/levenshtein.c
    lib/options/config.c
    lib/options/schema.c          # Config schema metadata (NEW)
    lib/options/rcu.c
    lib/options/builder.c         # Options builder API (NEW)
    lib/options/presets.c          # Preset option configs (NEW)
    lib/options/registry.c         # Central options registry (NEW)
    lib/options/parsers.c          # Custom enum parsers (NEW)
    lib/options/actions.c          # Action option callbacks (NEW)
    lib/options/layout.c           # Two-column layout helpers (NEW)
    lib/options/manpage.c          # Man page template generation (NEW)
    lib/options/manpage/resources.c # Man page resource abstraction layer (NEW)
    lib/options/manpage/parser.c   # Man page template parsing (NEW)
    lib/options/manpage/formatter.c # Groff/troff formatting utilities (NEW)
    lib/options/manpage/merger.c   # Man page merging and orchestration (NEW)
    lib/options/manpage/content/options.c     # OPTIONS section generator (NEW)
    lib/options/manpage/content/environment.c # ENVIRONMENT section generator (NEW)
    lib/options/manpage/content/usage.c       # USAGE section generator (NEW)
    lib/options/manpage/content/examples.c    # EXAMPLES section generator (NEW)
    lib/options/manpage/content/positional.c  # POSITIONAL ARGUMENTS section generator (NEW)
    lib/options/enums.c            # Enum value registry (NEW)
    lib/options/completions/completions.c  # Shell completion dispatcher (NEW)
    lib/options/completions/bash.c         # Bash completion generator (NEW)
    lib/options/completions/fish.c         # Fish completion generator (NEW)
    lib/options/completions/zsh.c          # Zsh completion generator (NEW)
    lib/options/completions/powershell.c   # PowerShell completion generator (NEW)
    lib/version.c
    # Discovery Service core (reused by discovery-service executable and tests)
    lib/discovery/session.c
    lib/discovery/database.c
    lib/discovery/identity.c
    lib/discovery/strings.c
    lib/discovery/adjectives.c
    lib/discovery/nouns.c
    # Discovery client (NAT detection and host negotiation)
    src/discovery/nat.c
    src/discovery/negotiate.c
    src/discovery/session.c
    src/discovery/webrtc.c
    # Add tomlc17 parser source
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/tomlc17/src/tomlc17.c
)

# Only include lock debugging runtime in non-release builds (when NDEBUG is not defined)
# debug/lock.c is wrapped in #ifndef NDEBUG, so it's safe to compile in release,
# but we exclude it for clarity and to avoid unnecessary compilation
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    list(APPEND CORE_SRCS lib/debug/lock.c)
    list(APPEND CORE_SRCS lib/debug/memory.c)
endif()

# Disable precompiled headers and static analyzers for tomlc17 (third-party code)
# Also disable implicit-conversion sanitizer check (line 128 has intentional int->size_t conversion)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/tomlc17/src/tomlc17.c
    PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    SKIP_LINTING ON
    COMPILE_FLAGS "-fno-sanitize=implicit-conversion"
)

# =============================================================================
# Data Structures Module
# =============================================================================
set(DATA_STRUCTURES_SRCS
    lib/ringbuffer.c
    lib/buffer_pool.c
    lib/thread_pool.c
)

# =============================================================================
# Session Library (reusable session components for discovery mode)
# =============================================================================
set(SESSION_SRCS
    lib/session/capture.c
    lib/session/display.c
    lib/session/help_screen.c
    lib/session/render.c
    lib/session/settings.c
    lib/session/audio.c
    lib/session/participant.c
    lib/session/host.c
    lib/session/keyboard_handler.c
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
    src/client/server.c
    src/client/protocol.c
    src/client/crypto.c
    src/client/display.c
    src/client/capture.c
    src/client/audio.c
    src/client/keepalive.c
    src/client/webrtc.c
    src/client/connection_attempt.c
    # Mirror mode sources
    src/mirror/main.c
    # Discovery-service mode sources
    src/discovery-service/main.c
    src/discovery-service/server.c
    src/discovery-service/signaling.c
    # Discovery mode sources (participant with dynamic host negotiation)
    src/discovery/main.c
)

# =============================================================================

