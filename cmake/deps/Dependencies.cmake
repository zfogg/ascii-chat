# =============================================================================
# Dependency Finding and Configuration
# =============================================================================
# Finds and configures all external dependencies for ascii-chat
#
# This file acts as a central orchestrator that includes modular dependency files.
# Each dependency is configured in its own separate file for better organization.
#
# Platform-specific dependency management:
#   - Windows: Uses vcpkg for package management
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Linux (musl): Dependencies built from source (see MuslDep.cmake in cmake/utils/)
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - VCPKG_ROOT: (Windows only) vcpkg installation path
#   - ASCIICHAT_DEPS_CACHE_DIR: Dependency cache directory
#
# Dependency modules included:
#   1. Vcpkg.cmake - vcpkg configuration (Windows only)
#   2. Zstd.cmake - zstd compression library
#   3. Libsodium.cmake - libsodium cryptography library
#   4. Portaudio.cmake - portaudio audio I/O library
#   5. BearSSL.cmake - BearSSL SSL/TLS library
#   6. Criterion.cmake - Criterion test framework (Unix only)
#   7. WindowsSDK.cmake - Windows SDK detection (Windows + Clang only)
#   8. PlatformLibraries.cmake - Platform-specific system libraries
# =============================================================================

# =============================================================================
# Windows: vcpkg Package Manager Configuration
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/deps/Vcpkg.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/deps/Libuv.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/deps/Utf8proc.cmake)

# =============================================================================
# Platform-Specific Configuration (EARLY - needed by other dependencies)
# =============================================================================

# Windows SDK detection and configuration (Windows + Clang only)
# Must be included early so WINDOWS_SDK_VERSION and WINDOWS_KITS_DIR are available
# for dependencies like WebRTC that need to pass library paths to subprojects
include(${CMAKE_SOURCE_DIR}/cmake/deps/WindowsSDK.cmake)

# =============================================================================
# Core Dependencies
# =============================================================================
# These dependencies are required on all platforms

# zstd - Compression library for frame data
include(${CMAKE_SOURCE_DIR}/cmake/deps/Zstd.cmake)

# libsodium - Cryptography library for encryption
include(${CMAKE_SOURCE_DIR}/cmake/deps/Libsodium.cmake)

# ALSA - Advanced Linux Sound Architecture (Linux only, needed by PortAudio especially for musl)
include(${CMAKE_SOURCE_DIR}/cmake/deps/ALSA.cmake)

# PortAudio - Audio I/O library for capture/playback
include(${CMAKE_SOURCE_DIR}/cmake/deps/Portaudio.cmake)

# Opus - Audio codec for real-time compression
include(${CMAKE_SOURCE_DIR}/cmake/deps/Opus.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/deps/FFTW.cmake)

# WebRTC Audio Processing - Production-grade echo cancellation with AEC3
include(${CMAKE_SOURCE_DIR}/cmake/deps/Abseil.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/deps/WebRTC.cmake)

# OpenSSL - SSL/TLS library (required by libdatachannel, libwebsockets, and TURN credentials)
include(${CMAKE_SOURCE_DIR}/cmake/deps/OpenSSL.cmake)

# BearSSL - SSL/TLS library for HTTPS key fetching
include(${CMAKE_SOURCE_DIR}/cmake/deps/BearSSL.cmake)

# libdatachannel - WebRTC DataChannels for P2P ACIP transport
include(${CMAKE_SOURCE_DIR}/cmake/deps/Libdatachannel.cmake)

# Zlib - Compression library (required by libwebsockets, especially for musl builds)
include(${CMAKE_SOURCE_DIR}/cmake/deps/Zlib.cmake)

# libwebsockets - WebSocket transport for browser clients (uses zlib for compression and mbedTLS for WSS)
include(${CMAKE_SOURCE_DIR}/cmake/deps/Libwebsockets.cmake)

# miniupnpc - UPnP/NAT-PMP for direct TCP without WebRTC (optional, graceful fallback)
include(${CMAKE_SOURCE_DIR}/cmake/deps/Miniupnpc.cmake)

# SQLite3 - Database for ACDS sessions and rate limiting
include(${CMAKE_SOURCE_DIR}/cmake/deps/SQLite3.cmake)

# PCRE2 - Regular expression library for URL validation
include(${CMAKE_SOURCE_DIR}/cmake/deps/PCRE2.cmake)

# mdns - mDNS service discovery library (requires patching to exclude main())
include(${CMAKE_SOURCE_DIR}/cmake/deps/Mdns.cmake)
configure_mdns()

# yyjson - Fast JSON library for structured logging
include(${CMAKE_SOURCE_DIR}/cmake/deps/Yyjson.cmake)
configure_yyjson()

# x264 - H.264/AVC encoder (needed by FFmpeg for libx264 support)
include(${CMAKE_SOURCE_DIR}/cmake/deps/X264.cmake)

# x265 - HEVC encoder (needed by FFmpeg for libx265 support)
include(${CMAKE_SOURCE_DIR}/cmake/deps/X265.cmake)

# FFmpeg - Media file decoding (includes HEVC codec support)
include(${CMAKE_SOURCE_DIR}/cmake/deps/FFmpeg.cmake)

# libexecinfo - Backtrace support (musl only)
include(${CMAKE_SOURCE_DIR}/cmake/deps/Libexecinfo.cmake)

# FreeType2 - Font rasterization for render-file
include(${CMAKE_SOURCE_DIR}/cmake/deps/FreeType2.cmake)

# Fontconfig - Font configuration for render-file
include(${CMAKE_SOURCE_DIR}/cmake/deps/Fontconfig.cmake)

# Libvterm - Terminal emulation for render-file (depends on FreeType2/Fontconfig)
include(${CMAKE_SOURCE_DIR}/cmake/deps/Libvterm.cmake)

# =============================================================================
# Test Dependencies
# =============================================================================
# Criterion test framework and related dependencies (Unix only)
include(${CMAKE_SOURCE_DIR}/cmake/deps/Criterion.cmake)

# =============================================================================
# Platform-Specific Configuration (LATE)
# =============================================================================

# Platform-specific system libraries (Windows, macOS frameworks, Linux libs)
include(${CMAKE_SOURCE_DIR}/cmake/deps/PlatformLibraries.cmake)

# Native telemetry is an explicit dependency; browser builds keep no-op hooks.
if(NOT EMSCRIPTEN)
    set(GOTEL_BUILD_TEST_SUPPORT ${BUILD_TESTS} CACHE BOOL "Build reusable telemetry test helpers" FORCE)
    set(GOTEL_WITH_SQLITE ON CACHE BOOL "Build SQLite telemetry adapter" FORCE)
    set(GOTEL_WITH_GRPC ON CACHE BOOL "Build native OTLP/gRPC telemetry transport" FORCE)
    set(GOTEL_UTF8PROC_TARGET utf8proc CACHE STRING "Use ascii-chat's utf8proc target" FORCE)
    add_library(asciichat-gotel-pcre2 INTERFACE)
    target_include_directories(asciichat-gotel-pcre2 INTERFACE ${PCRE2_INCLUDE_DIRS})
    target_link_libraries(asciichat-gotel-pcre2 INTERFACE ${PCRE2_LIBRARIES})
    set(GOTEL_PCRE2_TARGET asciichat-gotel-pcre2 CACHE STRING "Use ascii-chat's PCRE2 dependency" FORCE)
    add_library(asciichat-gotel-sqlite INTERFACE)
    if(SQLITE3_INCLUDE_DIRS)
        target_include_directories(asciichat-gotel-sqlite INTERFACE ${SQLITE3_INCLUDE_DIRS})
    endif()
    target_link_libraries(asciichat-gotel-sqlite INTERFACE ${SQLITE3_LIBRARIES})
    set(GOTEL_SQLITE_TARGET asciichat-gotel-sqlite CACHE STRING "Use ascii-chat's SQLite3 dependency" FORCE)
    set(_asciichat_parent_shared_libs ${BUILD_SHARED_LIBS})
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(BUILD_SHARED_LIBS OFF)
    else()
        set(BUILD_SHARED_LIBS ${ASCIICHAT_SHARED_DEPS})
    endif()
    add_subdirectory(${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/gotel/c
                     ${CMAKE_BINARY_DIR}/gotel)
    include(${CMAKE_SOURCE_DIR}/cmake/deps/OtelExporter.cmake)
    set(BUILD_SHARED_LIBS ${_asciichat_parent_shared_libs})
    add_compile_definitions(ASCIICHAT_WITH_OTEL=1 GOTEL_WITH_SQLITE=1)
    include_directories($<TARGET_PROPERTY:gotel,INTERFACE_INCLUDE_DIRECTORIES>
                        $<TARGET_PROPERTY:otlp_c,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
