cmake_minimum_required(VERSION 4.2)

# =============================================================================
# CMake Build Types
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/init/BuildTypes.cmake)
# INFO: CMAKE_BUILD_TYPE is now configured.

# =============================================================================
# Dependency Cache (must come early in the configuration process)
# =============================================================================
# Centralized dependency cache configuration
# This section must run AFTER BuildTypes.cmake is included (where CMAKE_BUILD_TYPE is set)
# Docker builds use .deps-cache/<Docker OS | docker>/, native builds use .deps-cache/
# Musl dependencies are stored in musl/ subdirectory

# Check if running in Docker (set by Dockerfile with ENV TESTING=1)
if(DEFINED ENV{DOCKER_OS})
    set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache/$ENV{DOCKER_OS}" CACHE PATH "Root directory for dependency cache (Docker $ENV{DOCKER_OS})")
elseif(EXISTS "/.dockerenv")
    set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache/docker" CACHE PATH "Root directory for dependency cache (Docker)")
else()
    set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache" CACHE PATH "Root directory for dependency cache (native)")
endif()

# Build-type-specific cache directory
set(ASCIICHAT_DEPS_CACHE_DIR "${ASCIICHAT_DEPS_CACHE_ROOT}/${CMAKE_BUILD_TYPE}" CACHE PATH "Dependency cache for current build type")

if (USE_MUSL)
    # Musl-specific cache directory
    set(ASCIICHAT_DEPS_CACHE_MUSL "${ASCIICHAT_DEPS_CACHE_DIR}/musl" CACHE PATH "Dependency cache for musl builds")
endif()

# Print dependency cache message (after Init.cmake defines colors)
message(STATUS "Using dependency cache: ${BoldCyan}${ASCIICHAT_DEPS_CACHE_DIR}${ColorReset}")

# =============================================================================
# Initialization
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/init/Init.cmake)

# =============================================================================
# Project Constants (must come before project() for DESCRIPTION/HOMEPAGE_URL)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/ProjectConstants.cmake)

# =============================================================================
# Version from git tags (single source of truth)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/install/Version.cmake)
version_detect()  # Sets PROJECT_VERSION_FROM_GIT

# =============================================================================
# Pre project() Configuration
# =============================================================================
# Enable experimental SPDX_LICENSE support in project() command (CMake 4.2+)
# This UUID is defined by CMake to gate experimental features - it must match exactly.
# See: https://github.com/Kitware/CMake/blob/master/Help/dev/experimental.rst
# If CMake changes the UUID in a future version, update this value accordingly.
set(CMAKE_EXPERIMENTAL_EXPORT_PACKAGE_INFO "b80be207-778e-46ba-8080-b23bba22639e")

include(${CMAKE_SOURCE_DIR}/cmake/platform/Musl.cmake)
configure_musl_pre_project()
include(${CMAKE_SOURCE_DIR}/cmake/compiler/LLVM.cmake)
configure_llvm_pre_project()

# =============================================================================
# Project Declaration
# =============================================================================
# Always enable C++ for Clang tooling (defer() and source instrumentation)
if(APPLE)
    set(PROJECT_LANGUAGES C CXX OBJC)
else()
    set(PROJECT_LANGUAGES C CXX)
endif()

project(${PROJECT_NAME_FULL}
    VERSION ${PROJECT_VERSION_FROM_GIT}
    SPDX_LICENSE "${PROJECT_LICENSE_SPDX}"
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
# Version targets (after project() - creates generate_version target)
# =============================================================================
version_setup_targets()

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
include(${CMAKE_SOURCE_DIR}/cmake/compiler/HeaderChecks.cmake)
configure_llvm_post_project()
fix_llvm_ranlib()
configure_musl_post_project()
include(${CMAKE_SOURCE_DIR}/cmake/platform/WindowsWorkarounds.cmake)
fix_windows_clang_linking()
find_msvc_libraries()
include(${CMAKE_SOURCE_DIR}/cmake/platform/Sanitizers.cmake)

# Optional CTest dashboard integration
if(ASCIICHAT_ENABLE_CTEST_DASHBOARD)
    include(CTest)
    include(${CMAKE_SOURCE_DIR}/cmake/test/CTest.cmake)
endif()

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

include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Mimalloc.cmake)

configure_include_directories()

# When USE_MUSL=ON, build all dependencies from source and cache them
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/MuslDependencies.cmake)

if(USE_MUSL)
    set(CMAKE_PREFIX_PATH "${MUSL_PREFIX}" ${CMAKE_PREFIX_PATH})
    message(STATUS "Will build static dependencies to: ${MUSL_PREFIX}")
endif()

# =============================================================================
# ascii-chat-Specific Stuff: Source code, Library modules, Headers, and Executable Config
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/targets/SourceFiles.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/tooling/Instrumentation.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/tooling/Defer.cmake)
ascii_instrumentation_prepare()
ascii_defer_prepare()

include(${CMAKE_SOURCE_DIR}/cmake/utils/TimerTargets.cmake)
# Configure archiver wrapper BEFORE creating any static libraries
include(${CMAKE_SOURCE_DIR}/cmake/init/ArchiverWrapper.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/targets/Libraries.cmake)

include(${CMAKE_SOURCE_DIR}/cmake/targets/TestLibs.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compiler/PCH.cmake)

include(${CMAKE_SOURCE_DIR}/cmake/targets/Executables.cmake)
ascii_instrumentation_finalize()
ascii_defer_finalize()

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

