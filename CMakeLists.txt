cmake_minimum_required(VERSION 3.28)

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
# Docker builds use .deps-cache/<arch>/ (amd64 or arm64), native builds use .deps-cache/
# Musl dependencies are stored in musl/ subdirectory

# Check if running in Docker (set by Dockerfile with ENV TESTING=1)
# DOCKER_OS should be set to architecture (amd64 or arm64) to prevent cross-arch binary conflicts
# Note: Use FORCE to ensure computed paths always reflect current CMAKE_SOURCE_DIR and CMAKE_BUILD_TYPE
if(DEFINED ENV{DOCKER_OS})
    set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache/$ENV{DOCKER_OS}" CACHE PATH "Root directory for dependency cache (Docker $ENV{DOCKER_OS})" FORCE)
elseif(EXISTS "/.dockerenv")
    message(WARNING "Running in Docker without DOCKER_OS set - using generic 'docker' cache path. Set DOCKER_OS=amd64 or DOCKER_OS=arm64 to avoid cross-arch conflicts.")
    set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache/docker" CACHE PATH "Root directory for dependency cache (Docker)" FORCE)
else()
    set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache" CACHE PATH "Root directory for dependency cache (native)" FORCE)
endif()

# Build-type-specific cache directory
# Note: Must use FORCE because this is computed from other variables that may change between runs
set(ASCIICHAT_DEPS_CACHE_DIR "${ASCIICHAT_DEPS_CACHE_ROOT}/${CMAKE_BUILD_TYPE}" CACHE PATH "Dependency cache for current build type" FORCE)

# Validate paths are not empty (which would create directories at root /)
if(NOT ASCIICHAT_DEPS_CACHE_ROOT OR ASCIICHAT_DEPS_CACHE_ROOT STREQUAL "" OR ASCIICHAT_DEPS_CACHE_ROOT STREQUAL "/")
    message(FATAL_ERROR "ASCIICHAT_DEPS_CACHE_ROOT is invalid: '${ASCIICHAT_DEPS_CACHE_ROOT}'\n"
                        "CMAKE_SOURCE_DIR: '${CMAKE_SOURCE_DIR}'\n"
                        "This should not happen - please report this bug.")
endif()
if(NOT ASCIICHAT_DEPS_CACHE_DIR OR ASCIICHAT_DEPS_CACHE_DIR STREQUAL "" OR ASCIICHAT_DEPS_CACHE_DIR MATCHES "^/$" OR ASCIICHAT_DEPS_CACHE_DIR MATCHES "^/[^/]+$")
    message(FATAL_ERROR "ASCIICHAT_DEPS_CACHE_DIR is invalid: '${ASCIICHAT_DEPS_CACHE_DIR}'\n"
                        "ASCIICHAT_DEPS_CACHE_ROOT: '${ASCIICHAT_DEPS_CACHE_ROOT}'\n"
                        "CMAKE_BUILD_TYPE: '${CMAKE_BUILD_TYPE}'\n"
                        "This should not happen - please report this bug.")
endif()

# Note: ASCIICHAT_DEPS_CACHE_MUSL is set in Musl.cmake's configure_musl_post_project()
# after USE_MUSL is properly defined. Don't set it here as USE_MUSL isn't defined yet.

# Print dependency cache message (after Init.cmake defines colors)
message(STATUS "Using dependency cache: ${BoldCyan}${ASCIICHAT_DEPS_CACHE_DIR}${ColorReset}")

# =============================================================================
# Initialization
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/ProjectConstants.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/init/Init.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/init/Options.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/install/Version.cmake)
version_detect()          # Sets PROJECT_VERSION_FROM_GIT (app version from v* tags)
library_version_detect()  # Sets ASCIICHAT_LIB_VERSION (library version from lib/v* tags)
include(${CMAKE_SOURCE_DIR}/cmake/init/BuildConfiguration.cmake)

# =============================================================================
# Pre project() Configuration
# =============================================================================
# Enable experimental SPDX_LICENSE support in project() command (CMake 4.2+)
# This UUID is defined by CMake to gate experimental features - it must match exactly.
# See: https://github.com/Kitware/CMake/blob/master/Help/dev/experimental.rst
# If CMake changes the UUID in a future version, update this value accordingly.
#set(CMAKE_EXPERIMENTAL_EXPORT_PACKAGE_INFO "b80be207-778e-46ba-8080-b23bba22639e")

include(${CMAKE_SOURCE_DIR}/cmake/platform/Musl.cmake)
configure_musl_pre_project()
include(${CMAKE_SOURCE_DIR}/cmake/compiler/LLVM.cmake)
configure_llvm_pre_project()

# Always enable C++ for Clang tooling (defer() and source instrumentation)
if(APPLE)
    set(PROJECT_LANGUAGES C CXX OBJC)
else()
    set(PROJECT_LANGUAGES C CXX)
endif()

# =============================================================================
# Project Declaration
# =============================================================================
#SPDX_LICENSE "${PROJECT_LICENSE_SPDX}"
project(${PROJECT_NAME_FULL}
    VERSION ${PROJECT_VERSION_FROM_GIT}
    DESCRIPTION "${PROJECT_DESCRIPTION_ONELINE}"
    HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}"
    LANGUAGES ${PROJECT_LANGUAGES}
)

# =============================================================================
# Post-project() Config (code that must be run after project())
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/init/PlatformDetection.cmake)
version_setup_targets()

# =============================================================================
# Install Prefix Configuration (must come after project() call)
# =============================================================================
# Default install prefix is /usr/local (CMake default)
# Homebrew formulas will override this with their own prefix during installation
# (e.g., /opt/homebrew for ARM Mac, /usr/local for Intel Mac)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    message(STATUS "Install prefix: ${BoldCyan}${CMAKE_INSTALL_PREFIX}${ColorReset} (default)")
endif()

# =============================================================================
# Generate paths.h with baked-in install prefix
# =============================================================================
configure_file(
    "${CMAKE_SOURCE_DIR}/lib/paths.h.in"
    "${CMAKE_BINARY_DIR}/generated/paths.h"
    @ONLY
)
message(STATUS "Generated paths.h with ASCIICHAT_INSTALL_PREFIX=${BoldYellow}${CMAKE_INSTALL_PREFIX}${ColorReset}")

# Only Ninja generator is supported - enforce this requirement
#if(NOT CMAKE_GENERATOR MATCHES "Ninja")
#    message(FATAL_ERROR
#        "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
#        "  ❌ Non-ninja Generator: ${CMAKE_GENERATOR}\n"
#        "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
#        "  This project only supports the Ninja build system.\n"
#        "  All other build systems are unsupported and untested.\n"
#        "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
#    )
#endif()

# =============================================================================
# Compiler config
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/compiler/CCache.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compiler/CompilerDetection.cmake)
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

# Configure build type after both CompilerFlags.cmake and Sanitizers.cmake are included
# (configure_build_type_post_project needs functions from both modules)
configure_build_type_post_project()

# Validate optional programs based on enabled options (from FindPrograms.cmake)
asciichat_validate_optional_programs()

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
endif()

# =============================================================================
# ascii-chat-Specific Stuff: Source code, Library modules, Headers, and Executable Config
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/targets/SourceFiles.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/tooling/Defer.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/tooling/Panic.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/tooling/Query.cmake)
# IMPORTANT: Defer must run BEFORE panic so that:
# 1. Defer analyzes clean original sources (correct scope analysis)
# 2. Panic then transforms defer's output (just adds logging)
ascii_defer_prepare()
ascii_panic_prepare()
# Query tool is independent of defer/panic - it doesn't do source transformation
ascii_query_prepare()

include(${CMAKE_SOURCE_DIR}/cmake/utils/TimerTargets.cmake)
# Configure archiver wrapper BEFORE creating any static libraries
include(${CMAKE_SOURCE_DIR}/cmake/init/ArchiverWrapper.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/targets/Libraries.cmake)

# Build panic runtime after Libraries.cmake creates ascii-chat-panic target
include(${CMAKE_SOURCE_DIR}/cmake/tooling/ToolingRuntime.cmake)
ascii_build_tooling_runtime()

include(${CMAKE_SOURCE_DIR}/cmake/targets/TestLibs.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compiler/PCH.cmake)

include(${CMAKE_SOURCE_DIR}/cmake/targets/Executables.cmake)
ascii_panic_finalize()
ascii_defer_finalize()
ascii_query_finalize()

include(${CMAKE_SOURCE_DIR}/cmake/targets/Tests.cmake)
# Optional CTest dashboard integration
if(ASCIICHAT_ENABLE_CTEST_DASHBOARD)
    include(CTest)
    include(${CMAKE_SOURCE_DIR}/cmake/test/CTest.cmake)
endif()

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
include(${CMAKE_SOURCE_DIR}/cmake/install/SystemConfig.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/targets/CustomTargets.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/utils/StatusMessages.cmake)

