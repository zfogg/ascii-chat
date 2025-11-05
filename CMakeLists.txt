cmake_minimum_required(VERSION 3.16)

# =============================================================================
# Dependency Cache Configuration (must come first)
# =============================================================================
# Centralized dependency cache configuration
# Docker builds use .deps-cache-docker/, native builds use .deps-cache/
# Musl dependencies are stored in musl/ subdirectory

# Check if running in Docker (set by Dockerfile with ENV TESTING=1)
if(DEFINED ENV{TESTING} AND "$ENV{TESTING}" STREQUAL "1")
    set(DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache-docker" CACHE PATH "Root directory for dependency cache (Docker)")
else()
    set(DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache" CACHE PATH "Root directory for dependency cache (native)")
endif()

# Build-type-specific cache directory
set(DEPS_CACHE_DIR "${DEPS_CACHE_ROOT}/${CMAKE_BUILD_TYPE}" CACHE PATH "Dependency cache for current build type")

# Musl-specific cache directory
set(DEPS_CACHE_MUSL "${DEPS_CACHE_DIR}/musl" CACHE PATH "Dependency cache for musl builds")

message(STATUS "Using dependency cache: ${DEPS_CACHE_DIR}")

# =============================================================================
# Initialization
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Init.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BuildTypes.cmake)

# =============================================================================
# Pre project() Configuration
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Musl.cmake)
configure_musl_pre_project()
include(${CMAKE_SOURCE_DIR}/cmake/LLVM.cmake)
configure_llvm_pre_project()

# =============================================================================
# Project Declaration
# =============================================================================
if(APPLE)
    set(PROJECT_LANGUAGES C OBJC)
else()
    set(PROJECT_LANGUAGES C)
endif()

project(ascii-chat
    VERSION 0.3.2
    DESCRIPTION "Real-time terminal-based video chat with ASCII art conversion"
    HOMEPAGE_URL "https://github.com/zfogg/ascii-chat"
    LANGUAGES ${PROJECT_LANGUAGES}
)

# =============================================================================
# Version Detection (after project() to override PROJECT_VERSION)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Version.cmake)

# =============================================================================
# Compiler config
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Options.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/PlatformDetection.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CCache.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CompilerDetection.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BuildConfiguration.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CompilerFlags.cmake)
configure_base_compiler_flags()
configure_llvm_post_project()
fix_llvm_ranlib()
configure_musl_post_project()
include(${CMAKE_SOURCE_DIR}/cmake/WindowsWorkarounds.cmake)
fix_windows_clang_linking()
find_msvc_libraries()
include(${CMAKE_SOURCE_DIR}/cmake/Sanitizers.cmake)

# Configure build type after both CompilerFlags.cmake and Sanitizers.cmake are included
# (configure_build_type_post_project needs functions from both modules)
configure_build_type_post_project()

# =============================================================================
# Hardware Acceleration Detection (must be before build type configuration)
# =============================================================================
# Note: CRC32 detection depends on SIMD detection, so SIMD must come first
include(${CMAKE_SOURCE_DIR}/cmake/SIMD.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CRC32.cmake)

# =============================================================================
# Dependencies
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Tomlc17.cmake)
configure_tomlc17()
include(${CMAKE_SOURCE_DIR}/cmake/LibsodiumBcryptPbkdf.cmake)
configure_libsodium_bcrypt_pbkdf()
include(${CMAKE_SOURCE_DIR}/cmake/Uthash.cmake)
configure_uthash()
include(${CMAKE_SOURCE_DIR}/cmake/Include.cmake)
configure_include_directories()

include(${CMAKE_SOURCE_DIR}/cmake/Mimalloc.cmake)

# When USE_MUSL=ON, build all dependencies from source and cache them
include(${CMAKE_SOURCE_DIR}/cmake/MuslDependencies.cmake)

if(USE_MUSL)
    message(STATUS "Will build static dependencies to: ${MUSL_PREFIX}")
    set(CMAKE_PREFIX_PATH "${MUSL_PREFIX}" ${CMAKE_PREFIX_PATH})
endif()

# =============================================================================
# ascii-chat-Specific Stuff: Source code, Library modules, Headers, and Executable Config
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/SourceFiles.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Libraries.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/PCH.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Executables.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Tests.cmake)

# =============================================================================
# Post-Build Processing (must come after Executables.cmake)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/PostBuild.cmake)
# Copy DLL dependencies to bin directory on Windows (for non-static builds)
if(WIN32)
    copy_windows_dlls(ascii-chat)
endif()

# =============================================================================
# Project meta stuff like documentation, installation packages, and custom targets like clang-tidy and clang-format
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Documentation.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Install.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CustomTargets.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/StatusMessages.cmake)
