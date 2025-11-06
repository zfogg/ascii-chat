cmake_minimum_required(VERSION 3.16)

# =============================================================================
# Dependency Cache Configuration (must come first)
# =============================================================================
# Centralized dependency cache configuration
# Docker builds use .deps-cache-docker/, native builds use .deps-cache/
# Musl dependencies are stored in musl/ subdirectory

# Check if running in Docker (set by Dockerfile with ENV TESTING=1)
if(DEFINED ENV{TESTING} AND "$ENV{TESTING}" STREQUAL "1")
    # Use OS-specific cache directory for Docker builds
    if(DEFINED ENV{DOCKER_OS})
        set(DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache-$ENV{DOCKER_OS}" CACHE PATH "Root directory for dependency cache (Docker $ENV{DOCKER_OS})")
    else()
        set(DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache-docker" CACHE PATH "Root directory for dependency cache (Docker)")
    endif()
else()
    set(DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache" CACHE PATH "Root directory for dependency cache (native)")
endif()

# Build-type-specific cache directory
set(DEPS_CACHE_DIR "${DEPS_CACHE_ROOT}/${CMAKE_BUILD_TYPE}" CACHE PATH "Dependency cache for current build type")

# Musl-specific cache directory
set(DEPS_CACHE_MUSL "${DEPS_CACHE_DIR}/musl" CACHE PATH "Dependency cache for musl builds")

# =============================================================================
# Initialization
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/init/Init.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/init/BuildTypes.cmake)

# Print dependency cache message (after Init.cmake defines colors)
message(STATUS "Using dependency cache: ${BoldCyan}${DEPS_CACHE_DIR}${ColorReset}")

# =============================================================================
# Project Constants (must come before project() for DESCRIPTION/HOMEPAGE_URL)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/ProjectConstants.cmake)

# =============================================================================
# Pre project() Configuration
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/platform/Musl.cmake)
configure_musl_pre_project()
include(${CMAKE_SOURCE_DIR}/cmake/compiler/LLVM.cmake)
configure_llvm_pre_project()

# =============================================================================
# Project Declaration
# =============================================================================
if(APPLE)
    set(PROJECT_LANGUAGES C OBJC)
else()
    set(PROJECT_LANGUAGES C)
endif()

project(${PROJECT_NAME_FULL}
    VERSION 0.3.2
    DESCRIPTION "${PROJECT_DESCRIPTION_ONELINE}"
    HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}"
    LANGUAGES ${PROJECT_LANGUAGES}
)

# =============================================================================
# Generator Validation (after project())
# =============================================================================
# Only Ninja generator is supported - enforce this requirement
if(NOT CMAKE_GENERATOR MATCHES "Ninja")
    message(FATAL_ERROR
        "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
        "  ❌ UNSUPPORTED GENERATOR: ${CMAKE_GENERATOR}\n"
        "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
        "\n"
        "  This project requires the Ninja build system.\n"
        "\n"
        "  To use Ninja:\n"
        "    1. Install Ninja:\n"
        "       - Windows: scoop install ninja\n"
        "       - macOS: brew install ninja\n"
        "       - Linux: sudo apt install ninja-build (or equivalent)\n"
        "\n"
        "    2. Configure with Ninja:\n"
        "       cmake -G Ninja -B build\n"
        "\n"
        "  Or use the build scripts:\n"
        "    - Windows: ./build.ps1\n"
        "    - Unix/macOS: cmake -B build && cmake --build build\n"
        "\n"
        "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
    )
endif()

# =============================================================================
# Version Detection (after project() to override PROJECT_VERSION)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/install/Version.cmake)

# =============================================================================
# Compiler config
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/init/Options.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/init/PlatformDetection.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compiler/CCache.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compiler/CompilerDetection.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/init/BuildConfiguration.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compiler/CompilerFlags.cmake)
configure_base_compiler_flags()
configure_llvm_post_project()
fix_llvm_ranlib()
configure_musl_post_project()
include(${CMAKE_SOURCE_DIR}/cmake/platform/WindowsWorkarounds.cmake)
fix_windows_clang_linking()
find_msvc_libraries()
include(${CMAKE_SOURCE_DIR}/cmake/platform/Sanitizers.cmake)

# Configure build type after both CompilerFlags.cmake and Sanitizers.cmake are included
# (configure_build_type_post_project needs functions from both modules)
configure_build_type_post_project()

# =============================================================================
# Hardware Acceleration Detection (must be before build type configuration)
# =============================================================================
# Note: CRC32 detection depends on SIMD detection, so SIMD must come first
include(${CMAKE_SOURCE_DIR}/cmake/compiler/SIMD.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compiler/CRC32.cmake)

# =============================================================================
# Dependencies
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Dependencies.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Tomlc17.cmake)
configure_tomlc17()
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/LibsodiumBcryptPbkdf.cmake)
configure_libsodium_bcrypt_pbkdf()
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Uthash.cmake)
configure_uthash()
include(${CMAKE_SOURCE_DIR}/cmake/utils/Include.cmake)
configure_include_directories()

include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Mimalloc.cmake)

# When USE_MUSL=ON, build all dependencies from source and cache them
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/MuslDependencies.cmake)

if(USE_MUSL)
    message(STATUS "Will build static dependencies to: ${MUSL_PREFIX}")
    set(CMAKE_PREFIX_PATH "${MUSL_PREFIX}" ${CMAKE_PREFIX_PATH})
endif()

# =============================================================================
# ascii-chat-Specific Stuff: Source code, Library modules, Headers, and Executable Config
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/targets/SourceFiles.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/utils/TimerTargets.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/targets/Libraries.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/targets/TestLibs.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compiler/PCH.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/targets/Executables.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/targets/Tests.cmake)

# =============================================================================
# Post-Build Processing (must come after Executables.cmake)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/utils/PostBuild.cmake)
# Copy DLL dependencies to bin directory on Windows (for non-static builds)
if(WIN32)
    copy_windows_dlls(ascii-chat)
endif()
# Check static linking for Release builds
check_static_linking(ascii-chat)

# =============================================================================
# Project meta stuff like documentation, installation packages, and custom targets like clang-tidy and clang-format
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/install/Documentation.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/install/Install.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/utils/CustomTargets.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/utils/StatusMessages.cmake)

