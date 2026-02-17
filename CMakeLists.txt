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
# Only compute cache root if not already passed from parent build
if(NOT DEFINED ASCIICHAT_DEPS_CACHE_ROOT OR ASCIICHAT_DEPS_CACHE_ROOT STREQUAL "")
    if(DEFINED ENV{DOCKER_OS})
        set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache/$ENV{DOCKER_OS}" CACHE PATH "Root directory for dependency cache (Docker $ENV{DOCKER_OS})" FORCE)
    elseif(EXISTS "/.dockerenv")
        message(WARNING "Running in Docker without DOCKER_OS set - using generic 'docker' cache path. Set DOCKER_OS=amd64 or DOCKER_OS=arm64 to avoid cross-arch conflicts.")
        set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache/docker" CACHE PATH "Root directory for dependency cache (Docker)" FORCE)
    else()
        set(ASCIICHAT_DEPS_CACHE_ROOT "${CMAKE_SOURCE_DIR}/.deps-cache" CACHE PATH "Root directory for dependency cache (native)" FORCE)
    endif()
endif()

# Build-type-specific cache directory
# Note: Only compute if not already passed from parent build (e.g., temp build for defer tool)
# This allows the defer tool's temp cmake build to use the same cache as the parent
if(NOT DEFINED ASCIICHAT_DEPS_CACHE_DIR OR ASCIICHAT_DEPS_CACHE_DIR STREQUAL "")
    set(ASCIICHAT_DEPS_CACHE_DIR "${ASCIICHAT_DEPS_CACHE_ROOT}/${CMAKE_BUILD_TYPE}" CACHE PATH "Dependency cache for current build type" FORCE)
endif()

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

# Disable LTO for musl builds globally to prevent TPOFF32 TLS relocation errors (LLVM issue #55909)
if(USE_MUSL)
    add_compile_options(-fno-lto)
    add_link_options(-fno-lto)
    message(STATUS "Disabling LTO globally for musl build (TPOFF32 TLS relocation fix)")
endif()

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

# Interprocedural optimization (LTO) support detection
# MUST be after project() when languages are enabled
include(CheckIPOSupported)
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    # Musl builds disable LTO via -fno-lto compile options added earlier to prevent TPOFF32 errors
    if(NOT USE_MUSL)
        check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
        if(IPO_SUPPORTED)
            set(ASCIICHAT_ENABLE_IPO TRUE CACHE INTERNAL "Enable IPO for release builds" FORCE)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
            message(STATUS "Interprocedural optimization ${BoldGreen}enabled${ColorReset} for ${CMAKE_BUILD_TYPE} builds")
        else()
            message(STATUS "Interprocedural optimization ${BoldYellow}disabled${ColorReset}: ${IPO_ERROR}")
        endif()
    else()
        set(ASCIICHAT_ENABLE_IPO FALSE CACHE INTERNAL "Disable IPO for musl builds" FORCE)
    endif()
endif()

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
    "${CMAKE_SOURCE_DIR}/include/ascii-chat/paths.h.in"
    "${CMAKE_BINARY_DIR}/generated/paths.h"
    @ONLY
)
# Copy generated paths.h back to include/ascii-chat/ for consistency
file(COPY "${CMAKE_BINARY_DIR}/generated/paths.h"
     DESTINATION "${CMAKE_SOURCE_DIR}/include/ascii-chat")
message(STATUS "Generated paths.h with ASCIICHAT_INSTALL_PREFIX=${BoldYellow}${CMAKE_INSTALL_PREFIX}${ColorReset}")

# Also copy generated version.h for consistency with angle-bracket includes
if(EXISTS "${CMAKE_BINARY_DIR}/generated/version.h")
    file(COPY "${CMAKE_BINARY_DIR}/generated/version.h"
         DESTINATION "${CMAKE_SOURCE_DIR}/include/ascii-chat")
endif()

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
# When USE_MUSL=ON, build all dependencies from source FIRST
# This must run before Dependencies.cmake so musl-built libraries (like OpenSSL)
# are available when dependency files (like Libdatachannel.cmake) try to find them
include(${CMAKE_SOURCE_DIR}/cmake/dependencies/MuslDependencies.cmake)

include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Dependencies.cmake)

include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Tomlc17.cmake)
configure_tomlc17()

include(${CMAKE_SOURCE_DIR}/cmake/dependencies/LibsodiumBcryptPbkdf.cmake)
configure_libsodium_bcrypt_pbkdf()

include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Uthash.cmake)
configure_uthash()

include(${CMAKE_SOURCE_DIR}/cmake/dependencies/FFmpeg.cmake)

include(${CMAKE_SOURCE_DIR}/cmake/utils/Include.cmake)

include(${CMAKE_SOURCE_DIR}/cmake/dependencies/Mimalloc.cmake)

configure_include_directories()

if(USE_MUSL)
    set(CMAKE_PREFIX_PATH "${MUSL_PREFIX}" ${CMAKE_PREFIX_PATH})
endif()

# =============================================================================
# ascii-chat-Specific Stuff: Source code, Library modules, Headers, and Executable Config
# =============================================================================
# Source files - recursive CMakeLists.txt (replaces SourceFiles.cmake)
# Each subdirectory defines its own sources and propagates via PARENT_SCOPE
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/util ${CMAKE_BINARY_DIR}/lib/util)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/platform ${CMAKE_BINARY_DIR}/lib/platform)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/crypto ${CMAKE_BINARY_DIR}/lib/crypto)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/video ${CMAKE_BINARY_DIR}/lib/video)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/audio ${CMAKE_BINARY_DIR}/lib/audio)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/network ${CMAKE_BINARY_DIR}/lib/network)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/tooling ${CMAKE_BINARY_DIR}/lib/tooling)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib ${CMAKE_BINARY_DIR}/lib-core)
add_subdirectory(${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/src)

# =============================================================================
# Embedded Resource Generation (Man Pages, etc.)
# =============================================================================
# Generate embedded resource files at build time for production binaries
# These are embedded at compile time for Release/RelWithDebInfo builds
# Development builds (Debug/Dev) read resources from filesystem for fast iteration

# Create output directory for embedded resources
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/embedded")

# Process template to substitute version variables (consistent for debug and release)
string(SUBSTRING "${PROJECT_VERSION_DATE}" 0 4 COPYRIGHT_YEAR_END)
set(MANPAGE_TEMPLATE_SOURCE "${CMAKE_SOURCE_DIR}/share/man/man1/ascii-chat.1.in")
set(MANPAGE_TEMPLATE_CONFIGURED "${CMAKE_BINARY_DIR}/share/man/man1/ascii-chat.1.in")
set(MANPAGE_TEMPLATE_EMBEDDED "${CMAKE_BINARY_DIR}/generated/embedded/manpage_template.c")

# Step 1: Configure template at build time (before embedding)
add_custom_command(
    OUTPUT "${MANPAGE_TEMPLATE_CONFIGURED}"
    COMMAND ${CMAKE_COMMAND}
        -D "PROJECT_VERSION=${PROJECT_VERSION}"
        -D "PROJECT_VERSION_DATE=${PROJECT_VERSION_DATE}"
        -D "CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -D "CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/utils/ConfigureTemplate.cmake"
    DEPENDS "${MANPAGE_TEMPLATE_SOURCE}"
    COMMENT "Configuring man page template"
    VERBATIM
)

# Step 2: Embed configured template using existing EmbedTextFile.cmake
add_custom_command(
    OUTPUT "${MANPAGE_TEMPLATE_EMBEDDED}"
    COMMAND ${CMAKE_COMMAND}
        -D "INPUT_FILE=${MANPAGE_TEMPLATE_CONFIGURED}"
        -D "OUTPUT_FILE=${MANPAGE_TEMPLATE_EMBEDDED}"
        -D "VARIABLE_NAME=embedded_manpage_template"
        -P "${CMAKE_SOURCE_DIR}/cmake/utils/EmbedTextFile.cmake"
    DEPENDS "${MANPAGE_TEMPLATE_CONFIGURED}"
    COMMENT "Embedding configured man page template"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    VERBATIM
)

# Add generated sources to CORE_SRCS for compilation
list(APPEND CORE_SRCS
    "${MANPAGE_TEMPLATE_EMBEDDED}"
)

# Mark generated files as generated (CMake housekeeping)
set_source_files_properties(
    "${MANPAGE_TEMPLATE_EMBEDDED}"
    PROPERTIES GENERATED TRUE
)

# Define resource paths for development builds
# Point to BINARY_DIR where configured templates are located (not SOURCE_DIR raw templates)
# These are used by embedded_resources.c to locate resources when running from the build directory
add_compile_definitions(
    ASCIICHAT_RESOURCE_DIR="${CMAKE_BINARY_DIR}"
)

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
include(${CMAKE_SOURCE_DIR}/cmake/targets/Libraries.cmake)

# Build panic runtime after Libraries.cmake creates ascii-chat-panic target
include(${CMAKE_SOURCE_DIR}/cmake/tooling/ToolingRuntime.cmake)
ascii_build_tooling_runtime()

include(${CMAKE_SOURCE_DIR}/cmake/targets/TestLibs.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/compiler/PCH.cmake)

include(${CMAKE_SOURCE_DIR}/cmake/targets/Executables.cmake)
# include(${CMAKE_SOURCE_DIR}/cmake/targets/ACDSExecutable.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/targets/WebAssembly.cmake)
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

