cmake_minimum_required(VERSION 3.16)

# =============================================================================
# Initialization (must come first)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Init.cmake)

# =============================================================================
# Build Type Configuration (must be early for musl decision)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/BuildTypes.cmake)
# configure_build_type_early() is called automatically in BuildTypes.cmake

# =============================================================================
# musl libc Pre-Project Configuration
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Musl.cmake)
configure_musl_pre_project()

# =============================================================================
# LLVM Configuration (must come before project())
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/LLVM.cmake)
configure_llvm_pre_project()

# =============================================================================
# Project Declaration
# =============================================================================

# Set languages based on platform
if(APPLE)
    set(PROJECT_LANGUAGES C OBJC)
else()
    set(PROJECT_LANGUAGES C)
endif()

project(ascii-chat
    VERSION 0.1.0
    DESCRIPTION "Real-time terminal-based video chat with ASCII art conversion"
    HOMEPAGE_URL "https://github.com/zfogg/ascii-chat"
    LANGUAGES ${PROJECT_LANGUAGES}
)

# Fix LLVM ranlib not being called automatically (must be after project())
fix_llvm_ranlib()

# =============================================================================
# Include modular CMake helpers
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Sanitizers.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CompilerFlags.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/WindowsWorkarounds.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Version.cmake)

# =============================================================================
# Windows-Specific Linking Fixes (must come after project())
# =============================================================================
fix_windows_clang_linking()

# =============================================================================
# LLVM Post-Project Configuration
# =============================================================================
configure_llvm_post_project()
find_msvc_libraries()

# =============================================================================
# Compiler Detection
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/CompilerDetection.cmake)

# =============================================================================
# Build Configuration
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/BuildConfiguration.cmake)

# =============================================================================
# Options Configuration
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Options.cmake)

# =============================================================================
# ccache Support (Compiler Cache for Faster Rebuilds)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/CCache.cmake)

# =============================================================================
# Platform Detection
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/PlatformDetection.cmake)

# =============================================================================
# Compiler Flags Configuration
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/CompilerFlags.cmake)
configure_base_compiler_flags()

# =============================================================================
# Hardware Acceleration Detection (must be before build type configuration)
# =============================================================================
# Note: CRC32 detection depends on SIMD detection, so SIMD must come first
include(${CMAKE_SOURCE_DIR}/cmake/SIMD.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CRC32.cmake)

# =============================================================================
# Build Type Configuration (using modular helper functions)
# =============================================================================
configure_build_type_post_project()

# =============================================================================
# musl libc Post-Project Configuration
# =============================================================================
configure_musl_post_project()

# =============================================================================
# mimalloc Memory Allocator (All Platforms)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Mimalloc.cmake)

# =============================================================================
# musl Dependencies (build all from source for musl libc)
# =============================================================================
# When USE_MUSL=ON, build all dependencies from source and cache them
include(${CMAKE_SOURCE_DIR}/cmake/MuslDependencies.cmake)

if(USE_MUSL)
    message(STATUS "Will build static dependencies to: ${MUSL_PREFIX}")
    set(CMAKE_PREFIX_PATH "${MUSL_PREFIX}" ${CMAKE_PREFIX_PATH})
endif()

# =============================================================================
# Find Dependencies (matching Makefile pkg-config approach)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)

# =============================================================================
# tomlc17 Patch Application
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Tomlc17.cmake)
configure_tomlc17()

# =============================================================================
# libsodium-bcrypt-pbkdf Patch Application
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/LibsodiumBcryptPbkdf.cmake)
configure_libsodium_bcrypt_pbkdf()

# =============================================================================
# uthash Configuration (header-only hash table library)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Uthash.cmake)
configure_uthash()

# =============================================================================
# Include Directories Configuration
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Include.cmake)
configure_include_directories()

# =============================================================================
# Source Files
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/SourceFiles.cmake)

# =============================================================================
# Libraries
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Libraries.cmake)

# =============================================================================
# Precompiled Headers Configuration
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/PCH.cmake)

# =============================================================================
# Executables
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Executables.cmake)

# =============================================================================
# Post-Build Processing (Release builds only)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/PostBuild.cmake)

# =============================================================================
# No Backwards Compatibility - Single Binary Only
# =============================================================================
# The unified binary is invoked as:
#   ./ascii-chat server [options...]
#   ./ascii-chat client [options...]

# =============================================================================
# Tests
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Tests.cmake)

# =============================================================================
# Installation
# =============================================================================

install(TARGETS ascii-chat
    RUNTIME DESTINATION bin
)

# =============================================================================
# Documentation
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/Documentation.cmake)

# =============================================================================
# Custom Targets
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/CustomTargets.cmake)

# =============================================================================
# Status Messages
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/StatusMessages.cmake)
