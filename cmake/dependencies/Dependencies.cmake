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
#   - Linux (musl): Dependencies built from source (see MuslDependencies.cmake)
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
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Vcpkg.cmake)

# =============================================================================
# Core Dependencies
# =============================================================================
# These dependencies are required on all platforms

# zstd - Compression library for frame data
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Zstd.cmake)

# libsodium - Cryptography library for encryption
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Libsodium.cmake)

# PortAudio - Audio I/O library for capture/playback
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Portaudio.cmake)

# BearSSL - SSL/TLS library for HTTPS key fetching
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/BearSSL.cmake)

# =============================================================================
# Test Dependencies
# =============================================================================
# Criterion test framework and related dependencies (Unix only)
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Criterion.cmake)

# =============================================================================
# Platform-Specific Configuration
# =============================================================================

# Windows SDK detection and configuration (Windows + Clang only)
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/WindowsSDK.cmake)

# Platform-specific system libraries (Windows, macOS frameworks, Linux libs)
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/PlatformLibraries.cmake)
