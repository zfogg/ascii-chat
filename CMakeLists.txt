cmake_minimum_required(VERSION 3.16)

# Set policy for ExternalProject timestamp handling (CMP0135)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# =============================================================================
# Dependency Cache Configuration (persistent across build/ deletions)
# =============================================================================
# Cache FetchContent dependencies outside build/ directory to avoid recompiling
# them every time build/ is deleted. Dependencies are compiled once and reused.
#
# Separate cache directories for different build configurations:
# - .deps-cache/<BuildType>/      : Normal glibc/system libc builds (per build type)
# - .deps-cache-musl/<BuildType>/ : musl libc builds (different ABI, per build type)
#
# Build types need separate caches because dependencies like mimalloc have different
# configurations (Debug: MI_DEBUG_FULL=ON, Release: MI_DEBUG_FULL=OFF)
#
# To force rebuild dependencies: rm -rf .deps-cache*

# Determine cache directory based on build configuration and build type
# Allow override via DEPS_CACHE_BASE environment variable (useful for Docker)
if(DEFINED ENV{DEPS_CACHE_BASE})
    set(DEPS_CACHE_BASE_DIR "$ENV{DEPS_CACHE_BASE}")
    message(STATUS "Using custom dependency cache base from environment: ${DEPS_CACHE_BASE_DIR}")
elseif(USE_MUSL)
    set(DEPS_CACHE_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps-cache-musl")
else()
    set(DEPS_CACHE_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps-cache")
endif()

set(FETCHCONTENT_BASE_DIR "${DEPS_CACHE_BASE_DIR}/${CMAKE_BUILD_TYPE}" CACHE PATH "FetchContent cache directory")
message(STATUS "Using dependency cache: ${FETCHCONTENT_BASE_DIR}")

# Set up vcpkg toolchain if available (must be before project())
if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        message(STATUS "Using vcpkg toolchain from environment: $ENV{VCPKG_ROOT}")
    endif()
endif()



# Use Ninja generator by default on all platforms for faster builds
# Only set Ninja if no generator was explicitly specified via -G flag
if(NOT CMAKE_GENERATOR AND NOT DEFINED CMAKE_GENERATOR_INTERNAL)
    find_program(NINJA_EXECUTABLE ninja)
    if(NINJA_EXECUTABLE)
        set(CMAKE_GENERATOR "Ninja" CACHE STRING "Build system generator" FORCE)
        message(STATUS "Using Ninja generator for faster builds")
    endif()
endif()

# On macOS, prefer gmake over make when using Unix Makefiles generator
if(APPLE AND CMAKE_GENERATOR MATCHES "Unix Makefiles")
    find_program(GMAKE_EXECUTABLE gmake)
    if(GMAKE_EXECUTABLE)
        set(CMAKE_MAKE_PROGRAM "${GMAKE_EXECUTABLE}" CACHE FILEPATH "Make program" FORCE)
        message(STATUS "Using gmake: ${GMAKE_EXECUTABLE}")
    endif()
endif()

# Speed up CMake's compiler tests by avoiding linking issues
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# =============================================================================
# musl libc Early Setup (MUST come before project() to configure compiler)
# =============================================================================

# Option to use musl libc (Linux only - musl is not compatible with macOS or Windows)
option(USE_MUSL "Use musl libc instead of glibc (Linux only)" OFF)

if(USE_MUSL)
    if(NOT UNIX OR APPLE OR WIN32)
        message(FATAL_ERROR "musl libc is only supported on Linux. macOS and Windows use their own C libraries.")
    endif()

    # Use clang directly with musl instead of musl-gcc wrapper
    # musl-gcc wrapper doesn't properly support -static-pie flag
    # See: https://github.com/rust-lang/rust/issues/95926
    find_program(CLANG_COMPILER clang)
    if(NOT CLANG_COMPILER)
        message(FATAL_ERROR "clang not found. Install clang for musl static-PIE support:\n"
                            "  Ubuntu/Debian: sudo apt install clang")
    endif()

    # Verify musl development files are installed
    if(NOT EXISTS "/usr/lib/x86_64-linux-musl/libc.a")
        message(FATAL_ERROR "musl development files not found. Install musl-dev:\n"
                            "  Arch Linux: sudo pacman -S musl\n"
                            "  Ubuntu/Debian: sudo apt install musl-tools musl-dev")
    endif()

    # Set compiler to clang BEFORE project()
    set(CMAKE_C_COMPILER "${CLANG_COMPILER}" CACHE FILEPATH "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "clang++" CACHE FILEPATH "CXX compiler" FORCE)

    # Set LLVM archiver and ranlib for proper archive handling
    # Search in LLVM installation directories
    find_program(LLVM_AR
        NAMES llvm-ar
        PATHS /usr/lib/llvm-20/bin /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin
        NO_DEFAULT_PATH
    )
    find_program(LLVM_RANLIB
        NAMES llvm-ranlib
        PATHS /usr/lib/llvm-20/bin /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin
        NO_DEFAULT_PATH
    )
    if(LLVM_AR)
        set(CMAKE_AR "${LLVM_AR}" CACHE FILEPATH "Archiver" FORCE)
        message(STATUS "Using LLVM archiver: ${LLVM_AR}")
    endif()
    if(LLVM_RANLIB)
        set(CMAKE_RANLIB "${LLVM_RANLIB}" CACHE FILEPATH "Ranlib" FORCE)
        message(STATUS "Using LLVM ranlib: ${LLVM_RANLIB}")
    endif()

    message(STATUS "Using clang with musl for static-PIE support")
endif()

# =============================================================================
# Homebrew LLVM Configuration (must come before project())
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/HomebrewLLVM.cmake)
configure_homebrew_llvm_pre_project()

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
# Homebrew LLVM Configuration (post-project library paths)
# =============================================================================
configure_homebrew_llvm_post_project()
find_msvc_libraries()

# =============================================================================
# Compiler detection: Respect environment variables first, then auto-detect
# =============================================================================

# 1. First priority: Respect CC environment variables if set
if(DEFINED ENV{CC} AND NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "$ENV{CC}" CACHE FILEPATH "C compiler from environment")
    message(STATUS "Using C compiler from CC environment variable: $ENV{CC}")
endif()

# 2. Respect CFLAGS, CPPFLAGS, LDFLAGS from environment
if(DEFINED ENV{CFLAGS})
    set(CMAKE_C_FLAGS "$ENV{CFLAGS} ${CMAKE_C_FLAGS}" CACHE STRING "C compiler flags")
    message(STATUS "Using CFLAGS from environment: $ENV{CFLAGS}")
endif()

if(DEFINED ENV{CPPFLAGS})
    # CPPFLAGS should be added to both C flags for preprocessor directives
    set(CMAKE_C_FLAGS "$ENV{CPPFLAGS} ${CMAKE_C_FLAGS}" CACHE STRING "C compiler flags")
    message(STATUS "Using CPPFLAGS from environment: $ENV{CPPFLAGS}")
endif()

if(DEFINED ENV{LDFLAGS})
    set(CMAKE_EXE_LINKER_FLAGS "$ENV{LDFLAGS} ${CMAKE_EXE_LINKER_FLAGS}" CACHE STRING "Linker flags")
    set(CMAKE_SHARED_LINKER_FLAGS "$ENV{LDFLAGS} ${CMAKE_SHARED_LINKER_FLAGS}" CACHE STRING "Shared linker flags")
    message(STATUS "Using LDFLAGS from environment: $ENV{LDFLAGS}")
endif()

# 3. Set default compilers to Clang if not explicitly set
# This ensures the project works with no arguments: cmake -B build && cmake --build build
if(NOT CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
    find_program(CLANG_EXECUTABLE clang)
    if(CLANG_EXECUTABLE)
        set(CMAKE_C_COMPILER "${CLANG_EXECUTABLE}" CACHE FILEPATH "Default C compiler")
        message(STATUS "Set default C compiler to: ${CLANG_EXECUTABLE}")
    else()
        message(WARNING "clang not found in PATH. You may need to install LLVM/Clang.")
    endif()
endif()

# 4. Set default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build" FORCE)
    message(STATUS "Set default build type to Debug")
endif()

# 4. Handle ccache wrapper if present
if(CMAKE_C_COMPILER)
    # Check if compiler is a ccache wrapper
    if(CMAKE_C_COMPILER MATCHES "ccache")
        # Try to find the real compiler
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-prog-name=clang
            OUTPUT_VARIABLE REAL_C_COMPILER
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(REAL_C_COMPILER AND NOT REAL_C_COMPILER STREQUAL "clang")
            message(STATUS "Detected ccache wrapper, real compiler: ${REAL_C_COMPILER}")
            # Don't override CMAKE_C_COMPILER but note for sanitizer setup
            set(CCACHE_DETECTED TRUE)
        endif()
    endif()
endif()

# Generate compile_commands.json for IDE/tool integration (clangd, VSCode, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Automatically copy or symlink compile_commands.json to project root
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    if(NOT CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
        # Only do this if we're doing an out-of-source build
        set(COMPILE_COMMANDS_SOURCE "${CMAKE_BINARY_DIR}/compile_commands.json")
        set(COMPILE_COMMANDS_DEST "${CMAKE_SOURCE_DIR}/compile_commands.json")

        if(WIN32)
            # On Windows, create a junction/symlink at configure time
            # This way the file will be available as soon as Ninja generates it
            # Remove existing file/link first
            if(EXISTS "${COMPILE_COMMANDS_DEST}")
                file(REMOVE "${COMPILE_COMMANDS_DEST}")
            endif()

            # Try to create a symlink (requires developer mode or admin on Windows)
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E create_symlink
                    "${COMPILE_COMMANDS_SOURCE}"
                    "${COMPILE_COMMANDS_DEST}"
                RESULT_VARIABLE symlink_result
                ERROR_QUIET
            )

            if(symlink_result EQUAL 0)
                message(STATUS "Created symlink for compile_commands.json in project root")
            else()
                # Symlink failed, we'll copy it as a custom target instead
                message(STATUS "Will copy compile_commands.json to project root after build")
                add_custom_target(copy_compile_commands ALL
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${COMPILE_COMMANDS_SOURCE}"
                        "${COMPILE_COMMANDS_DEST}"
                    COMMENT "Copying compile_commands.json to project root"
                    VERBATIM
                )
            endif()
        else()
            # On Unix-like systems, symlinks work reliably
            if(EXISTS "${COMPILE_COMMANDS_DEST}")
                file(REMOVE "${COMPILE_COMMANDS_DEST}")
            endif()

            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E create_symlink
                    "${COMPILE_COMMANDS_SOURCE}"
                    "${COMPILE_COMMANDS_DEST}"
                RESULT_VARIABLE symlink_result
            )

            if(symlink_result EQUAL 0)
                message(STATUS "Created symlink for compile_commands.json in project root")
            endif()
        endif()
    endif()
endif()

# Clear Windows-specific CMake flags for native Clang builds
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    # CMake automatically adds MSVC runtime flags even for Clang, clear them
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreaded "")
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDLL "")
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebug "")
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebugDLL "")

    # Clear the runtime library setting that adds -D_DEBUG -D_DLL -D_MT
    set(CMAKE_MSVC_RUNTIME_LIBRARY "")

    message(STATUS "Cleared CMake Windows runtime flags for Clang-only build")
endif()

# =============================================================================
# Configuration
# =============================================================================

# Define colors for terminal output
if(NOT WIN32)
    string(ASCII 27 Esc)
    set(ColorReset "${Esc}[m")
    set(ColorBold "${Esc}[1m")
    set(Red "${Esc}[31m")
    set(Green "${Esc}[32m")
    set(Yellow "${Esc}[33m")
    set(Blue "${Esc}[34m")
    set(Magenta "${Esc}[35m")
    set(Cyan "${Esc}[36m")
    set(White "${Esc}[37m")
    set(BoldRed "${Esc}[1;31m")
    set(BoldGreen "${Esc}[1;32m")
    set(BoldYellow "${Esc}[1;33m")
    set(BoldBlue "${Esc}[1;34m")
    set(BoldMagenta "${Esc}[1;35m")
    set(BoldCyan "${Esc}[1;36m")
    set(BoldWhite "${Esc}[1;37m")
else()
    set(ColorReset "")
    set(ColorBold "")
    set(Red "")
    set(Green "")
    set(Yellow "")
    set(Blue "")
    set(Magenta "")
    set(Cyan "")
    set(White "")
    set(BoldRed "")
    set(BoldGreen "")
    set(BoldYellow "")
    set(BoldBlue "")
    set(BoldMagenta "")
    set(BoldCyan "")
    set(BoldWhite "")
endif()

# Detect CPU cores for parallel builds
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    execute_process(COMMAND sysctl -n hw.logicalcpu OUTPUT_VARIABLE CPU_CORES OUTPUT_STRIP_TRAILING_WHITESPACE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    execute_process(COMMAND nproc OUTPUT_VARIABLE CPU_CORES OUTPUT_STRIP_TRAILING_WHITESPACE)
elseif(WIN32)
    # Windows: Use environment variable or wmic
    if(DEFINED ENV{NUMBER_OF_PROCESSORS})
        set(CPU_CORES $ENV{NUMBER_OF_PROCESSORS})
    else()
        execute_process(COMMAND wmic cpu get NumberOfLogicalProcessors /value
                       OUTPUT_VARIABLE CPU_INFO OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "NumberOfLogicalProcessors=([0-9]+)" _ ${CPU_INFO})
        if(CMAKE_MATCH_1)
            set(CPU_CORES ${CMAKE_MATCH_1})
        else()
            set(CPU_CORES 4)
        endif()
    endif()
else()
    set(CPU_CORES 4)
endif()

# Set parallel build level automatically if not already set
if(NOT DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} ${CPU_CORES})
endif()
message(STATUS "Parallel build jobs: ${CPU_CORES}")

# C standard selection - intelligently detect the best available standard
# Try C23 first (newer compilers), fall back to C2X for compatibility
if(NOT CMAKE_C_STANDARD)
    # Check if compiler supports C23 standard
    include(CheckCCompilerFlag)

    # First try -std=c23 (newer compilers like Clang 18+, GCC 14+)
    check_c_compiler_flag("-std=c23" COMPILER_SUPPORTS_C23)

    if(COMPILER_SUPPORTS_C23)
        set(CMAKE_C_STANDARD 23)
        message(STATUS "Compiler supports C23 standard")
    else()
        # Fall back to C2X for older compilers (GitHub Actions Ubuntu 22.04)
        set(CMAKE_C_STANDARD 23)  # CMake will translate this to c2x if c23 isn't available
        message(STATUS "Using C2X standard (C23 preview)")
    endif()
endif()

set(CMAKE_C_STANDARD_REQUIRED ON)
# Enable GNU extensions for POSIX compatibility (strdup, getopt_long, etc.)
# With C23, this gives us -std=gnu23 or -std=gnu2x
set(CMAKE_C_EXTENSIONS ON)


# Option to build tests
option(BUILD_TESTS "Build test executables" ON)

# Build type (matches Makefile modes)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type: Debug, Dev, Release, Coverage" FORCE)
endif()

# Valid build types (matching Makefile)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Dev" "Release" "Coverage" "TSan")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# =============================================================================
# Early Options (must come before ccache and build type configuration)
# =============================================================================

# Note: USE_MUSL option is declared before project() (line ~41)
# so compiler can be configured before CMake tests it

# Option to use mimalloc (must be defined early so build type flags can check it)
option(USE_MIMALLOC "Use mimalloc high-performance allocator" ON)

# musl builds configuration
if(USE_MUSL)
    set(USE_CCACHE OFF CACHE BOOL "Disable ccache when using musl" FORCE)
    # Note: Sanitizers are automatically disabled for musl+mimalloc builds
    # in configure_sanitizers() - no need to change build type
endif()

# =============================================================================
# ccache Support (Compiler Cache for Faster Rebuilds)
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/CCache.cmake)

# =============================================================================
# Platform Detection
# =============================================================================

# Initialize all platform variables to FALSE
set(PLATFORM_DARWIN FALSE)
set(PLATFORM_LINUX FALSE)
set(PLATFORM_WINDOWS FALSE)
set(PLATFORM_POSIX FALSE)

# Detect platform specifics like the Makefile
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM_DARWIN TRUE)
    set(PLATFORM_POSIX TRUE)

    # Apple Silicon and Rosetta detection
    execute_process(
        COMMAND sysctl -n hw.optional.arm64
        OUTPUT_VARIABLE IS_APPLE_SILICON
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND sysctl -n sysctl.proc_translated
        OUTPUT_VARIABLE IS_ROSETTA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(NOT IS_APPLE_SILICON)
        set(IS_APPLE_SILICON 0)
    endif()
    if(NOT IS_ROSETTA)
        set(IS_ROSETTA 0)
    endif()

    # Force arm64 when building natively on Apple Silicon
    if(IS_APPLE_SILICON EQUAL 1 AND NOT IS_ROSETTA EQUAL 1)
        set(CMAKE_OSX_ARCHITECTURES arm64)
    endif()

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_LINUX TRUE)
    set(PLATFORM_POSIX TRUE)
    set(IS_APPLE_SILICON 0)
    set(IS_ROSETTA 0)

elseif(WIN32)
    add_definitions(-D_WIN32 -DWIN32_LEAN_AND_MEAN)
    set(PLATFORM_WINDOWS TRUE)
    set(IS_APPLE_SILICON 0)
    set(IS_ROSETTA 0)

    # Windows SDK version detection will be handled by the comprehensive detection below

    # Detect Windows architecture generically
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
        set(PLATFORM_WINDOWS_ARM64 TRUE)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM")
        set(PLATFORM_WINDOWS_ARM TRUE)
    else()
        set(PLATFORM_WINDOWS_X64 TRUE)
    endif()
else()
    set(PLATFORM_POSIX TRUE)
endif()

# Enable GNU extensions for POSIX functions (matches Makefile)
if(PLATFORM_POSIX AND NOT WIN32)
    add_definitions(-D_GNU_SOURCE)
endif()

# =============================================================================
# Compiler Flags (matching Makefile logic)
# =============================================================================

# Base warning flags for Clang/GCC compilers
add_compile_options(-Wall -Wextra)

# Enable frame pointers for better backtraces (required for musl + libexecinfo)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-fno-omit-frame-pointer)
    message(STATUS "Frame pointers enabled for backtraces")
endif()

# Platform-specific compiler flags
if(PLATFORM_DARWIN)
    # Let CMake handle SDK paths automatically for cross-generator compatibility
    # No manual SDK configuration needed
endif()

# Windows-specific: No special compiler flags needed
# Struct packing is handled via #pragma pack directives in windows_compat.h

# Include directories (matches Makefile)
include_directories(
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)

# =============================================================================
# Hardware Acceleration Detection (must be before build type configuration)
# =============================================================================
# Note: CRC32 detection depends on SIMD detection, so SIMD must come first
include(${CMAKE_SOURCE_DIR}/cmake/SIMD.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/CRC32.cmake)

# =============================================================================
# Build Type Configuration (using modular helper functions)
# =============================================================================
# Build type specific flags and sanitizer configuration

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug mode WITH sanitizers (default)
    configure_debug_memory(${USE_MIMALLOC} ${USE_MUSL})
    configure_debug_build_flags("Debug")

    # Configure sanitizers (automatically handles mimalloc conflicts)
    configure_sanitizers(${USE_MIMALLOC} "Debug")

    # Platform-specific sanitizer runtime fixes
    fix_macos_asan_runtime()
    copy_asan_runtime_dll()

elseif(CMAKE_BUILD_TYPE STREQUAL "Dev")
    # Dev mode - debug WITHOUT sanitizers (faster iteration)
    configure_debug_memory(${USE_MIMALLOC} ${USE_MUSL})
    configure_debug_build_flags("Dev")
    # No sanitizers in Dev mode

elseif(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    # Coverage build with instrumentation
    configure_coverage_flags()

elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Optimized release build
    configure_release_flags(${PLATFORM_DARWIN} ${PLATFORM_LINUX} ${IS_ROSETTA} ${IS_APPLE_SILICON} ${ENABLE_CRC32_HW})

elseif(CMAKE_BUILD_TYPE STREQUAL "Sanitize")
    # Sanitize mode - focus on AddressSanitizer
    add_definitions(-DDEBUG_MEMORY)
    configure_debug_build_flags("Debug")
    configure_sanitizers(${USE_MIMALLOC} "Sanitize")
    copy_asan_runtime_dll()

elseif(CMAKE_BUILD_TYPE STREQUAL "TSan")
    # Thread Sanitizer mode
    add_definitions(-DDEBUG_MEMORY -DDEBUG_THREADS)
    configure_sanitizers(${USE_MIMALLOC} "TSan")
endif()

# musl libc Additional Configuration
# =============================================================================
# Note: Compiler setup is done before project() (line ~37)
# This section handles linker flags and other post-project() configuration

if(USE_MUSL)
    # Use -static-pie flag to build fully static PIE binaries with musl
    # Combines static linking with Position Independent Executable for security (ASLR)
    # Using clang directly with musl for proper static-PIE support (musl-gcc wrapper doesn't support it)
    # See: https://github.com/rust-lang/rust/issues/95926

    # Find GCC version for libgcc.a
    execute_process(
        COMMAND gcc -dumpversion
        OUTPUT_VARIABLE GCC_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX REPLACE "\\.[0-9]+\\.[0-9]+$" "" GCC_MAJOR_VERSION "${GCC_VERSION}")

    set(MUSL_LIBDIR "/usr/lib/x86_64-linux-musl")
    set(GCC_LIBDIR "/usr/lib/gcc/x86_64-linux-gnu/${GCC_MAJOR_VERSION}")

    # Configure clang to use musl with static-PIE
    # Based on: https://wiki.debian.org/musl
    add_compile_options(
        -target x86_64-linux-musl
    )

    # Linker flags for static linking with musl
    # Use crt1.o for regular static (not rcrt1.o which is for static-PIE)
    set(CMAKE_EXE_LINKER_FLAGS
        "-target x86_64-linux-musl -static -nostdlib -L${MUSL_LIBDIR} ${MUSL_LIBDIR}/crt1.o ${MUSL_LIBDIR}/crti.o"
    )
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

    # Link with musl libc and libgcc at the end
    link_libraries(c "${GCC_LIBDIR}/libgcc.a")
    add_link_options("${MUSL_LIBDIR}/crtn.o")

    # Disable precompiled headers
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "Disable PCH for musl" FORCE)

    message(STATUS "musl C library enabled (static linking with clang)")
    message(STATUS "Using GCC ${GCC_VERSION} libgcc from: ${GCC_LIBDIR}")
    message(STATUS "")
    message(STATUS "NOTE: Static linking enabled - will build static dependencies with musl")

    # Include ExternalProject module for building dependencies
    include(ExternalProject)

    # Extract real compiler path (skip ccache if present)
    if(CMAKE_C_COMPILER MATCHES "ccache")
        # CMAKE_C_COMPILER is "/usr/bin/ccache /usr/bin/musl-gcc" - extract the real compiler
        string(REGEX REPLACE ".* " "" MUSL_CC "${CMAKE_C_COMPILER}")
    else()
        set(MUSL_CC "${CMAKE_C_COMPILER}")
    endif()
    message(STATUS "Using compiler for musl dependencies: ${MUSL_CC}")

    # Note: zlib-musl, libsodium-musl, alsa-lib-musl, portaudio-musl, and libexecinfo-musl are now built in MuslDependencies.cmake

    # Note: JACK is complex and optional - we'll skip it for static musl builds
endif()

# =============================================================================
# mimalloc Memory Allocator (All Platforms)
# =============================================================================

# Note: USE_MIMALLOC option is defined early in the file (line ~559)
# so build type configuration can check it before adding DEBUG_MEMORY

if(USE_MIMALLOC)
    message(STATUS "Configuring mimalloc memory allocator...")

    # Download and build mimalloc from GitHub (or use cached version)
    message(STATUS "Downloading mimalloc from GitHub...")

    include(FetchContent)

    FetchContent_Declare(
        mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG v2.1.7  # Latest stable v2.x release
        GIT_SHALLOW TRUE
        # Use persistent cache directories (survives build/ deletion)
        SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/mimalloc-src"
        BINARY_DIR "${FETCHCONTENT_BASE_DIR}/mimalloc-build"
    )

    # Configure mimalloc build options
    set(MI_BUILD_SHARED OFF CACHE BOOL "Build shared library")
    set(MI_BUILD_STATIC ON CACHE BOOL "Build static library")
    set(MI_BUILD_OBJECT OFF CACHE BOOL "Build object library")
    set(MI_BUILD_TESTS OFF CACHE BOOL "Build test executables")
    set(MI_OVERRIDE ON CACHE BOOL "Override malloc/free globally")
    set(MI_INSTALL_TOPLEVEL OFF CACHE BOOL "Install in top-level")

    # Set debug level for mimalloc based on build type
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
        set(MI_DEBUG_FULL ON CACHE BOOL "Full debug mode for mimalloc")
    endif()

    # Platform-specific mimalloc options
    if(WIN32)
        set(MI_WIN_REDIRECT ON CACHE BOOL "Redirect malloc on Windows")
    endif()

    # musl-specific options for static-PIE compatibility
    if(USE_MUSL)
        # Disable malloc override for musl to avoid symbol conflicts with libc.a
        # musl's static libc.a includes malloc/free, and overriding causes multiple definition errors
        set(MI_OVERRIDE OFF CACHE BOOL "Don't override malloc for musl" FORCE)

        # Use local-dynamic TLS model instead of initial-exec for static-PIE compatibility
        # mimalloc's default initial-exec TLS doesn't work with static-PIE on musl
        # See: https://github.com/microsoft/mimalloc/issues/644
        set(MI_LOCAL_DYNAMIC_TLS ON CACHE BOOL "Use local-dynamic TLS for musl static-PIE")
        set(MI_LIBC_MUSL ON CACHE BOOL "Build for musl libc")
        message(STATUS "mimalloc: Disabled malloc override for musl (avoiding symbol conflicts)")
        message(STATUS "mimalloc: Enabled musl-specific TLS options for static-PIE compatibility")
    endif()

    # Save current output directory settings
    set(_SAVED_ARCHIVE_OUTPUT_DIR ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
    set(_SAVED_LIBRARY_OUTPUT_DIR ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

    # Set output directories to persistent cache (so library survives build/ deletion)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${FETCHCONTENT_BASE_DIR}/mimalloc-build/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${FETCHCONTENT_BASE_DIR}/mimalloc-build/lib")

    # Populate and build mimalloc
    FetchContent_MakeAvailable(mimalloc)

    # Restore original output directory settings
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_SAVED_ARCHIVE_OUTPUT_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_SAVED_LIBRARY_OUTPUT_DIR})

    # Fix C23 compatibility issues with mimalloc
    if(TARGET mimalloc-static)
        # Add errno.h include for C23 compatibility
        target_compile_options(mimalloc-static PRIVATE -include errno.h)
        message(STATUS "Added errno.h include for mimalloc C23 compatibility")
    endif()

    # For musl builds, set REALGCC environment for mimalloc build
    if(USE_MUSL AND REAL_GCC AND TARGET mimalloc-static)
        set_target_properties(mimalloc-static PROPERTIES
            COMPILE_OPTIONS ""
            RULE_LAUNCH_COMPILE "env REALGCC=${REAL_GCC}"
        )
        message(STATUS "Configured mimalloc build environment with REALGCC=${REAL_GCC}")
    endif()

    message(STATUS "Built mimalloc from source (v2.1.7)")
    set(MIMALLOC_LIBRARIES mimalloc-static)

    # Define USE_MIMALLOC for all source files so they can use mi_malloc/mi_free directly
    add_compile_definitions(USE_MIMALLOC)

    message(STATUS "mimalloc will override malloc/free globally")
    message(STATUS "Your existing SAFE_MALLOC macros will automatically use mimalloc")
endif()

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

# Platform-specific package management and library linking
if(WIN32)
    # Native Windows with Clang - use vcpkg
    # Setup vcpkg paths if available
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT $ENV{VCPKG_ROOT})
        set(CMAKE_PREFIX_PATH "${VCPKG_ROOT}/installed/x64-windows" ${CMAKE_PREFIX_PATH})
        include_directories("${VCPKG_ROOT}/installed/x64-windows/include")
        link_directories("${VCPKG_ROOT}/installed/x64-windows/lib")
        message(STATUS "Using vcpkg from: ${VCPKG_ROOT}")
    endif()

    # Find packages using vcpkg (toolchain already configured at top of file)
    # Find zlib - vcpkg provides both debug and optimized libraries
    find_package(ZLIB REQUIRED)
    message(STATUS "Found ZLIB: ${ZLIB_VERSION_STRING}")
    # ZLIB_LIBRARIES from vcpkg contains generator expressions that automatically
    # select the debug library (zlibd.lib) for Debug builds and the optimized
    # library (zlib.lib) for Release builds

    # Find libsodium - try finding it directly to avoid import issues
    find_library(LIBSODIUM_LIBRARY NAMES libsodium sodium
                PATHS "${VCPKG_ROOT}/installed/x64-windows/lib")
    find_path(LIBSODIUM_INCLUDE_DIR NAMES sodium.h
             PATHS "${VCPKG_ROOT}/installed/x64-windows/include")
    if(LIBSODIUM_LIBRARY AND LIBSODIUM_INCLUDE_DIR)
        set(LIBSODIUM_LIBRARIES ${LIBSODIUM_LIBRARY})
        set(LIBSODIUM_INCLUDE_DIRS ${LIBSODIUM_INCLUDE_DIR})
        set(LIBSODIUM_FOUND TRUE)
        message(STATUS "Found libsodium: ${LIBSODIUM_LIBRARY}")
    else()
        message(WARNING "Could not find libsodium - will continue without encryption")
        set(LIBSODIUM_FOUND FALSE)
        set(LIBSODIUM_LIBRARIES "")
        set(LIBSODIUM_INCLUDE_DIRS "")
    endif()

    # Find libgcrypt - try finding it directly to avoid import issues
    # Note: libgcrypt is not available on Windows via vcpkg (requires MinGW which we don't support)
    # GPG key support will be unavailable on Windows
    find_library(LIBGCRYPT_LIBRARY NAMES libgcrypt gcrypt
                PATHS "${VCPKG_ROOT}/installed/x64-windows/lib")
    find_path(LIBGCRYPT_INCLUDE_DIR NAMES gcrypt.h
             PATHS "${VCPKG_ROOT}/installed/x64-windows/include")
    if(LIBGCRYPT_LIBRARY AND LIBGCRYPT_INCLUDE_DIR)
        set(LIBGCRYPT_LIBRARIES ${LIBGCRYPT_LIBRARY})
        set(LIBGCRYPT_INCLUDE_DIRS ${LIBGCRYPT_INCLUDE_DIR})
        set(LIBGCRYPT_FOUND TRUE)
        message(STATUS "Found libgcrypt: ${LIBGCRYPT_LIBRARY}")
    else()
        message(WARNING "Could not find libgcrypt - GPG Ed25519 signature verification will be disabled")
        message(WARNING "  libgcrypt is not available on Windows (requires MinGW). GPG keys will not work on Windows.")
        set(LIBGCRYPT_FOUND FALSE)
        set(LIBGCRYPT_LIBRARIES "")
        set(LIBGCRYPT_INCLUDE_DIRS "")
    endif()

    # Try to find BearSSL (system install from Docker or pkg-config)
    # First check for system-installed version (e.g., from Docker image)
    find_library(BEARSSL_SYSTEM_LIB NAMES bearssl libbearssl bearssls
                 PATHS /usr/local/lib /usr/lib
                 NO_DEFAULT_PATH)
    find_path(BEARSSL_SYSTEM_INC NAMES bearssl.h
              PATHS /usr/local/include /usr/include
              NO_DEFAULT_PATH)

    if(BEARSSL_SYSTEM_LIB AND BEARSSL_SYSTEM_INC)
        # Use system-installed BearSSL (from Docker)
        add_library(bearssl_static STATIC IMPORTED)
        set_target_properties(bearssl_static PROPERTIES
            IMPORTED_LOCATION "${BEARSSL_SYSTEM_LIB}"
        )
        target_include_directories(bearssl_static INTERFACE "${BEARSSL_SYSTEM_INC}")
        set(BEARSSL_LIBRARIES bearssl_static)
        set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SYSTEM_INC}")
        set(BEARSSL_FOUND TRUE)
        message(STATUS "Using system BearSSL library: ${BEARSSL_SYSTEM_LIB}")
    # Fall back to building from submodule
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/bearssl")
        set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/bearssl")
        # Build to cache directory to keep platform-specific builds separate
        set(BEARSSL_BUILD_DIR "${DEPS_CACHE_BASE_DIR}/bearssl-build")
        # Windows uses bearssls.lib, Unix uses libbearssl.a
        set(BEARSSL_LIB "${BEARSSL_BUILD_DIR}/bearssls.lib")

        file(MAKE_DIRECTORY "${BEARSSL_BUILD_DIR}")

        # Apply Windows+Clang patch to BearSSL (fixes header conflicts with clang-cl)
        # This needs to happen at configure time to ensure the source is patched
        set(BEARSSL_PATCH_FILE "${CMAKE_SOURCE_DIR}/cmake/bearssl-windows-clang.patch")
        execute_process(
            COMMAND git -C "${BEARSSL_SOURCE_DIR}" diff --quiet src/inner.h
            RESULT_VARIABLE BEARSSL_PATCH_NEEDED
            OUTPUT_QUIET ERROR_QUIET
        )
        if(NOT BEARSSL_PATCH_NEEDED EQUAL 0)
            message(STATUS "Applying BearSSL patch for Windows+Clang compatibility")
            execute_process(
                COMMAND git -C "${BEARSSL_SOURCE_DIR}" apply --ignore-whitespace "${BEARSSL_PATCH_FILE}"
                RESULT_VARIABLE BEARSSL_PATCH_RESULT
                OUTPUT_QUIET ERROR_QUIET
            )
            if(NOT BEARSSL_PATCH_RESULT EQUAL 0)
                message(WARNING "Failed to apply BearSSL patch (may already be applied)")
            endif()
        endif()

        # Find build tools
        find_program(NMAKE_EXECUTABLE nmake REQUIRED)
        find_program(CLANG_CL_EXECUTABLE clang-cl REQUIRED)
        find_program(LLVM_LIB_EXECUTABLE llvm-lib REQUIRED)

        # Add custom command to build BearSSL if library is missing
        # This creates a build rule that Ninja/Make can use to rebuild the library
        add_custom_command(
            OUTPUT "${BEARSSL_LIB}"
            COMMAND ${CMAKE_COMMAND} -E echo "Building BearSSL library with nmake..."
            COMMAND "${NMAKE_EXECUTABLE}" "CC=${CLANG_CL_EXECUTABLE}" "AR=${LLVM_LIB_EXECUTABLE}" lib
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEARSSL_SOURCE_DIR}/build/bearssls.lib" "${BEARSSL_LIB}"
            WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
            COMMENT "Building BearSSL library to cache: ${BEARSSL_BUILD_DIR}"
            VERBATIM
        )

        # Add custom target that depends on the library
        add_custom_target(bearssl_build DEPENDS "${BEARSSL_LIB}")

        # Create an imported library that links to the custom command output
        add_library(bearssl_static STATIC IMPORTED GLOBAL)
        set_target_properties(bearssl_static PROPERTIES
            IMPORTED_LOCATION "${BEARSSL_LIB}"
        )
        target_include_directories(bearssl_static INTERFACE
            "${BEARSSL_SOURCE_DIR}/inc"
        )
        # Make sure the library is built before anything tries to link against it
        add_dependencies(bearssl_static bearssl_build)

        set(BEARSSL_LIBRARIES bearssl_static)
        set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SOURCE_DIR}/inc")
        set(BEARSSL_FOUND TRUE)
        message(STATUS "BearSSL configured with rebuild rule: ${BEARSSL_LIB}")
    else()
        message(WARNING "BearSSL submodule not found - GitHub/GitLab key fetching will be disabled")
        set(BEARSSL_FOUND FALSE)
        set(BEARSSL_LIBRARIES "")
        set(BEARSSL_INCLUDE_DIRS "")
    endif()
else()
    # Unix/Linux/macOS use pkg-config (matches Makefile)
    find_package(PkgConfig REQUIRED)

    # Core dependencies (matching PKG_CONFIG_LIBS in Makefile)
    # On macOS, prefer Homebrew zlib over system zlib for consistency
    if(APPLE)
        # Check for Homebrew zlib first
        if(EXISTS "/usr/local/opt/zlib/lib/pkgconfig/zlib.pc")
            set(ENV{PKG_CONFIG_PATH} "/usr/local/opt/zlib/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
            message(STATUS "Using Homebrew zlib from /usr/local/opt/zlib")
        elseif(EXISTS "/opt/homebrew/opt/zlib/lib/pkgconfig/zlib.pc")
            set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/zlib/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
            message(STATUS "Using Homebrew zlib from /opt/homebrew/opt/zlib")
        endif()
    endif()

    # Skip pkg-config when using musl - dependencies are built from source
    if(NOT USE_MUSL)
        pkg_check_modules(ZLIB REQUIRED zlib)
        pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
        pkg_check_modules(LIBSODIUM REQUIRED libsodium)
        # libgcrypt is optional - GPG Ed25519 signature verification
        pkg_check_modules(LIBGCRYPT libgcrypt)
        if(NOT LIBGCRYPT_FOUND)
            message(WARNING "libgcrypt not found - GPG Ed25519 signature verification will be disabled")
        endif()
    endif()

    # Try to find BearSSL (system install from Docker or pkg-config)
    # First check for system-installed version (e.g., from Docker image)
    find_library(BEARSSL_SYSTEM_LIB NAMES bearssl libbearssl
                 PATHS /usr/local/lib /usr/lib
                 NO_DEFAULT_PATH)
    find_path(BEARSSL_SYSTEM_INC NAMES bearssl.h
              PATHS /usr/local/include /usr/include
              NO_DEFAULT_PATH)

    if(BEARSSL_SYSTEM_LIB AND BEARSSL_SYSTEM_INC)
        # Use system-installed BearSSL (from Docker)
        add_library(bearssl_static STATIC IMPORTED)
        set_target_properties(bearssl_static PROPERTIES
            IMPORTED_LOCATION "${BEARSSL_SYSTEM_LIB}"
        )
        target_include_directories(bearssl_static INTERFACE "${BEARSSL_SYSTEM_INC}")
        set(BEARSSL_LIBRARIES bearssl_static)
        set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SYSTEM_INC}")
        set(BEARSSL_FOUND TRUE)
        message(STATUS "Using system BearSSL library: ${BEARSSL_SYSTEM_LIB}")
    # Fall back to building from submodule
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/bearssl")
        set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/bearssl")
        # Build to cache directory to keep platform-specific builds separate
        set(BEARSSL_BUILD_DIR "${DEPS_CACHE_BASE_DIR}/bearssl-build")
        set(BEARSSL_LIB "${BEARSSL_BUILD_DIR}/libbearssl.a")

        file(MAKE_DIRECTORY "${BEARSSL_BUILD_DIR}")

        # Add custom command to build BearSSL if library is missing
        # This creates a build rule that Ninja/Make can use to rebuild the library
        add_custom_command(
            OUTPUT "${BEARSSL_LIB}"
            COMMAND ${CMAKE_COMMAND} -E echo "Building BearSSL library with make..."
            COMMAND make CC=${CMAKE_C_COMPILER} AR=${CMAKE_AR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEARSSL_SOURCE_DIR}/build/libbearssl.a" "${BEARSSL_LIB}"
            WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
            COMMENT "Building BearSSL library to cache: ${BEARSSL_BUILD_DIR}"
            VERBATIM
        )

        # Add custom target that depends on the library
        add_custom_target(bearssl_build DEPENDS "${BEARSSL_LIB}")

        # Create an imported library that links to the custom command output
        add_library(bearssl_static STATIC IMPORTED GLOBAL)
        set_target_properties(bearssl_static PROPERTIES
            IMPORTED_LOCATION "${BEARSSL_LIB}"
        )
        target_include_directories(bearssl_static INTERFACE
            "${BEARSSL_SOURCE_DIR}/inc"
        )
        # Make sure the library is built before anything tries to link against it
        add_dependencies(bearssl_static bearssl_build)

        set(BEARSSL_LIBRARIES bearssl_static)
        set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SOURCE_DIR}/inc")
        set(BEARSSL_FOUND TRUE)
        message(STATUS "BearSSL configured with rebuild rule: ${BEARSSL_LIB}")
    else()
        message(WARNING "BearSSL submodule not found - GitHub/GitLab key fetching will be disabled")
        set(BEARSSL_FOUND FALSE)
        set(BEARSSL_LIBRARIES "")
        set(BEARSSL_INCLUDE_DIRS "")
    endif()
endif()

# Test dependencies (matching TEST_PKG_CONFIG_LIBS in Makefile)
# Criterion detection is outside the USE_MUSL block so tests work with musl
# Disable tests for musl builds - Criterion test framework requires glibc
# Tests can be run with standard glibc builds instead
if(USE_MUSL)
    set(BUILD_TESTS OFF)
    message(STATUS "Tests disabled for musl builds (Criterion requires glibc)")
endif()

# Windows doesn't use pkg-config, so skip Criterion detection on Windows
# Tests are primarily Unix-based (Criterion requires pkg-config)
if(BUILD_TESTS AND NOT WIN32)
    pkg_check_modules(CRITERION criterion)
endif()

# Windows SDK detection for native Clang builds
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(STATUS "Setting up native Windows build with Clang")

    # Find Windows SDK - check Visual Studio preferences first, then standard locations
    set(WINDOWS_KIT_PATHS)

    # Check if Visual Studio has a preferred SDK version via MSBuild
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        set(PREFERRED_SDK_VERSION ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION})
        message(STATUS "Visual Studio preferred SDK version: ${PREFERRED_SDK_VERSION}")
    endif()

    # Check standard Windows Kits locations
    list(APPEND WINDOWS_KIT_PATHS
        "C:/Program Files (x86)/Windows Kits/10"
        "C:/Program Files/Windows Kits/10"
        "$ENV{WindowsSdkDir}"
    )

    foreach(KIT_PATH IN LISTS WINDOWS_KIT_PATHS)
        if(EXISTS "${KIT_PATH}" AND NOT WINDOWS_KITS_DIR)
            set(WINDOWS_KITS_DIR "${KIT_PATH}")
            break()
        endif()
    endforeach()

    if(WINDOWS_KITS_DIR)
        # Find SDK version - prioritize Visual Studio preferred version if available
        file(GLOB SDK_VERSIONS "${WINDOWS_KITS_DIR}/Lib/10.*")
        if(SDK_VERSIONS)
            # Check if Visual Studio has a preferred SDK version and it's available
            if(PREFERRED_SDK_VERSION)
                set(PREFERRED_SDK_PATH "${WINDOWS_KITS_DIR}/Lib/${PREFERRED_SDK_VERSION}")
                if(EXISTS "${PREFERRED_SDK_PATH}")
                    set(WINDOWS_SDK_VERSION ${PREFERRED_SDK_VERSION})
                    message(STATUS "Using Visual Studio preferred SDK version: ${WINDOWS_SDK_VERSION}")
                else()
                    message(STATUS "Visual Studio preferred SDK version ${PREFERRED_SDK_VERSION} not found, selecting latest available")
                endif()
            endif()

            # If no preferred version or preferred version not found, select latest
            if(NOT WINDOWS_SDK_VERSION)
                # Sort SDK versions properly by parsing version numbers
                set(SORTED_SDK_VERSIONS)
                foreach(sdk_path ${SDK_VERSIONS})
                    get_filename_component(sdk_version ${sdk_path} NAME)
                    # Extract version components (e.g., "10.0.22621.0" -> 10, 0, 22621, 0)
                    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)" version_match "${sdk_version}")
                    if(version_match)
                        set(major ${CMAKE_MATCH_1})
                        set(minor ${CMAKE_MATCH_2})
                        set(build ${CMAKE_MATCH_3})
                        set(revision ${CMAKE_MATCH_4})
                        # Create sortable version string (pad with zeros for proper sorting)
                        string(LENGTH ${build} build_len)
                        string(LENGTH ${revision} revision_len)
                        if(build_len LESS 5)
                            string(REPEAT "0" 5 build_pad)
                            math(EXPR pad_len "5 - ${build_len}")
                            string(SUBSTRING ${build_pad} 0 ${pad_len} build_pad)
                            set(build "${build_pad}${build}")
                        endif()
                        if(revision_len LESS 3)
                            string(REPEAT "0" 3 revision_pad)
                            math(EXPR rev_pad_len "3 - ${revision_len}")
                            string(SUBSTRING ${revision_pad} 0 ${rev_pad_len} revision_pad)
                            set(revision "${revision_pad}${revision}")
                        endif()
                        set(sort_key "${major}.${minor}.${build}.${revision}")
                        list(APPEND SORTED_SDK_VERSIONS "${sort_key}|${sdk_version}")
                    endif()
                endforeach()

                # Sort by the sortable key
                list(SORT SORTED_SDK_VERSIONS COMPARE NATURAL ORDER DESCENDING)
                list(GET SORTED_SDK_VERSIONS 0 LATEST_SDK_ENTRY)
                string(REGEX REPLACE "^[^|]*\\|" "" WINDOWS_SDK_VERSION "${LATEST_SDK_ENTRY}")
                message(STATUS "Selected latest available SDK version: ${WINDOWS_SDK_VERSION}")
            endif()

            # Validate SDK has required components
            if(EXISTS "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/ucrt" AND
               EXISTS "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/ucrt/x64")

                # Architecture detection
                if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                    set(WIN_ARCH x64)
                else()
                    set(WIN_ARCH x86)
                endif()

                # Add Windows SDK paths
                include_directories(
                    "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/ucrt"
                    "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/um"
                    "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/shared"
                )

                link_directories(
                    "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/ucrt/${WIN_ARCH}"
                    "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/um/${WIN_ARCH}"
                )

                # Find MSVC runtime libraries (required for Clang with lld-link)
                set(MSVC_BASE_PATHS
                    "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Tools/MSVC"
                    "C:/Program Files (x86)/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
                    "C:/Program Files (x86)/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC"
                    "C:/Program Files (x86)/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC"
                    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC"
                )

                set(MSVC_LIB_DIR)
                foreach(MSVC_BASE IN LISTS MSVC_BASE_PATHS)
                    if(EXISTS "${MSVC_BASE}")
                        file(GLOB MSVC_VERSIONS "${MSVC_BASE}/*")
                        if(MSVC_VERSIONS)
                            # Sort to get latest version
                            list(SORT MSVC_VERSIONS COMPARE NATURAL ORDER DESCENDING)
                            list(GET MSVC_VERSIONS 0 MSVC_LATEST)
                            set(MSVC_LIB_CANDIDATE "${MSVC_LATEST}/lib/${WIN_ARCH}")
                            if(EXISTS "${MSVC_LIB_CANDIDATE}")
                                set(MSVC_LIB_DIR "${MSVC_LIB_CANDIDATE}")
                                get_filename_component(MSVC_VERSION_DIR "${MSVC_LATEST}" NAME)
                                message(STATUS "Found MSVC runtime libraries: ${MSVC_VERSION_DIR}")
                                break()
                            endif()
                        endif()
                    endif()
                endforeach()

                if(MSVC_LIB_DIR)
                    link_directories("${MSVC_LIB_DIR}")
                else()
                    message(WARNING "\n"
                        "================================================================================\n"
                        "WARNING: MSVC Runtime Libraries Not Found\n"
                        "================================================================================\n"
                        "Clang requires MSVC runtime libraries (libcmt.lib, oldnames.lib) for linking.\n"
                        "Release builds may fail without these libraries.\n"
                        "\n"
                        "To install MSVC runtime libraries:\n"
                        "  1. Open Visual Studio Installer\n"
                        "  2. Click 'Modify' on your Visual Studio installation\n"
                        "  3. Go to 'Individual Components' tab\n"
                        "  4. Search for 'MSVC v143 - VS 2022 C++ x64/x86 build tools'\n"
                        "  5. Select the component and click 'Modify' to install\n"
                        "================================================================================")
                endif()

                message(STATUS "Found Windows SDK ${WINDOWS_SDK_VERSION}")

                # Validate Windows SDK version
                string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" SDK_VERSION_MATCH "${WINDOWS_SDK_VERSION}")
                if(SDK_VERSION_MATCH)
                    set(SDK_MAJOR ${CMAKE_MATCH_1})
                    set(SDK_MINOR ${CMAKE_MATCH_2})

                    # Check for minimum version 10.0
                    if(SDK_MAJOR LESS 10)
                        message(FATAL_ERROR "\n"
                            "================================================================================\n"
                            "ERROR: Insufficient Windows SDK Version\n"
                            "================================================================================\n"
                            "ASCII-Chat requires Windows SDK 10.0 or higher for Media Foundation APIs.\n"
                            "Current version detected: ${WINDOWS_SDK_VERSION}\n"
                            "\n"
                            "To install Windows SDK 10.0 or higher:\n"
                            "\n"
                            "METHOD 1 - Visual Studio Installer (Recommended):\n"
                            "  1. Open Visual Studio Installer\n"
                            "  2. Click 'Modify' on your Visual Studio installation\n"
                            "  3. Go to 'Individual Components' tab\n"
                            "  4. Search for 'Windows 10 SDK' or 'Windows 11 SDK'\n"
                            "  5. Select the latest version (10.0.22621.0 or newer)\n"
                            "  6. Click 'Modify' to install\n"
                            "\n"
                            "METHOD 2 - Standalone Installer:\n"
                            "  1. Download from: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/\n"
                            "  2. Run the installer and select 'Windows SDK for Desktop C++ Development'\n"
                            "\n"
                            "METHOD 3 - Command Line (winget):\n"
                            "  winget install Microsoft.WindowsSDK\n"
                            "\n"
                            "After installation, restart your command prompt and try building again.\n"
                            "================================================================================")
                    else()
                        message(STATUS "Windows SDK version ${WINDOWS_SDK_VERSION} detected - sufficient for ASCII-Chat")
                    endif()
                else()
                    message(WARNING "Could not parse Windows SDK version: ${WINDOWS_SDK_VERSION}")
                endif()
            else()
                message(WARNING "Windows SDK ${WINDOWS_SDK_VERSION} found but missing required components (ucrt)")
            endif()
        else()
            message(WARNING "\n"
                "================================================================================\n"
                "WARNING: Could not detect Windows SDK version\n"
                "================================================================================\n"
                "ASCII-Chat requires Windows SDK 10.0 or higher for Media Foundation APIs.\n"
                "If you encounter build errors related to missing headers (mfapi.h, mfidl.h),\n"
                "please install Windows SDK 10.0 or higher using one of these methods:\n"
                "\n"
                "METHOD 1 - Visual Studio Installer (Recommended):\n"
                "  1. Open Visual Studio Installer\n"
                "  2. Click 'Modify' on your Visual Studio installation\n"
                "  3. Go to 'Individual Components' tab\n"
                "  4. Search for 'Windows 10 SDK' or 'Windows 11 SDK'\n"
                "  5. Select the latest version (10.0.22621.0 or newer)\n"
                "  6. Click 'Modify' to install\n"
                "\n"
                "METHOD 2 - Standalone Installer:\n"
                "  Download from: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/\n"
                "\n"
                "METHOD 3 - Command Line (winget):\n"
                "  winget install Microsoft.WindowsSDK\n"
                "================================================================================")
        endif()
    else()
        message(WARNING "\n"
            "================================================================================\n"
            "WARNING: Could not find Windows Kits directory\n"
            "================================================================================\n"
            "ASCII-Chat requires Windows SDK 10.0 or higher for Media Foundation APIs.\n"
            "Please install Windows SDK 10.0 or higher using one of these methods:\n"
            "\n"
            "METHOD 1 - Visual Studio Installer (Recommended):\n"
            "  1. Open Visual Studio Installer\n"
            "  2. Click 'Modify' on your Visual Studio installation\n"
            "  3. Go to 'Individual Components' tab\n"
            "  4. Search for 'Windows 10 SDK' or 'Windows 11 SDK'\n"
            "  5. Select the latest version (10.0.22621.0 or newer)\n"
            "  6. Click 'Modify' to install\n"
            "\n"
            "METHOD 2 - Standalone Installer:\n"
            "  Download from: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/\n"
            "\n"
            "METHOD 3 - Command Line (winget):\n"
            "  winget install Microsoft.WindowsSDK\n"
            "================================================================================")
    endif()

    # Override CMake's Windows-Clang platform settings that add -nostartfiles -nostdlib
    # These flags prevent linking to oldnames.lib and cause link errors
    # CMake automatically adds these in the Windows-Clang toolchain but they're incorrect for our use case
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Clear the CMake flags that cause -nostartfiles -nostdlib to be added
        set(CMAKE_C_CREATE_SHARED_LIBRARY "<CMAKE_C_COMPILER> <CMAKE_SHARED_LIBRARY_C_FLAGS> <LANGUAGE_COMPILE_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> <SONAME_FLAG><TARGET_SONAME> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
        set(CMAKE_C_CREATE_SHARED_MODULE "${CMAKE_C_CREATE_SHARED_LIBRARY}")
        set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

        # Disable runtime library selection that adds libcmt.lib and oldnames.lib
        # These are legacy compatibility libraries not needed for modern C code with Clang
        set(CMAKE_MSVC_RUNTIME_LIBRARY "")
        set(CMAKE_C_STANDARD_LIBRARIES "")

        # Also clear any runtime library flags that might be set by CMake for Windows
        set(CMAKE_C_STANDARD_LIBRARIES_INIT "")
        set(CMAKE_C_IMPLICIT_LINK_LIBRARIES "")

        # Don't add any additional runtime libraries - let Clang use its defaults
        # The key fix was removing -nostartfiles -nostdlib, now standard linking should work

        message(STATUS "Overrode CMake Windows-Clang linking rules to prevent -nostartfiles -nostdlib")
        message(STATUS "Removed oldnames.lib from standard libraries (legacy compatibility library not needed)")
    endif()

endif()
# Find Windows-specific libraries
if(WIN32)
    # Standard Windows system libraries for native builds
    set(WS2_32_LIB ws2_32)
    set(USER32_LIB user32)
    set(ADVAPI32_LIB advapi32)
    set(DBGHELP_LIB dbghelp)
    set(MF_LIB mf)
    set(MFPLAT_LIB mfplat)
    set(MFREADWRITE_LIB mfreadwrite)
    set(MFUUID_LIB mfuuid)
    set(OLE32_LIB ole32)

    # Find PortAudio - vcpkg provides appropriate library for each config
    find_package(portaudio CONFIG REQUIRED)
    set(PORTAUDIO_LIBRARIES portaudio)
    set(PORTAUDIO_FOUND TRUE)
else()
    # Platform-specific libraries (matching Makefile logic)
    if(PLATFORM_DARWIN)
        find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
        find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
        find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
        find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    elseif(PLATFORM_LINUX)
        find_package(Threads REQUIRED)

        # Linux library search paths (matches Makefile)
        link_directories(/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu)

        # Check for JACK (optional, matches Makefile)
        pkg_check_modules(JACK jack)

        # Additional Linux test dependencies (matching Makefile complex setup)
        if(CRITERION_FOUND)
            # Protobuf-C
            pkg_check_modules(PROTOBUF_C libprotobuf-c)
            if(NOT PROTOBUF_C_FOUND)
                find_library(PROTOBUF_C_LIBRARIES protobuf-c)
            endif()

            # Nanopb
            pkg_check_modules(NANOPB nanopb)
            if(NOT NANOPB_FOUND)
                find_library(NANOPB_LIBRARIES protobuf-nanopb PATHS /usr/lib/x86_64-linux-gnu)
            endif()

            # Boxfort (sandboxing for criterion)
            pkg_check_modules(BOXFORT boxfort)
            if(NOT BOXFORT_FOUND)
                find_library(BOXFORT_LIBRARIES boxfort)
            endif()

            # Optional: nanomsg, libgit2
            pkg_check_modules(NANOMSG nanomsg)
            pkg_check_modules(LIBGIT2 libgit2)

            # GSSAPI/Kerberos support
            pkg_check_modules(KRB5_GSSAPI krb5-gssapi)
            if(NOT KRB5_GSSAPI_FOUND)
                pkg_check_modules(KRB5_GSSAPI mit-krb5-gssapi)
            endif()
            if(NOT KRB5_GSSAPI_FOUND)
                pkg_check_modules(LIBSSH2 libssh2)
            endif()
        endif()
    endif()
endif()

# =============================================================================
# Include Directories (matching Makefile)
# =============================================================================

# Base include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)

# Add dependency include directories (matching pkg-config approach)
if(WIN32)
    if(DEFINED ENV{VCPKG_ROOT})
        include_directories($ENV{VCPKG_ROOT}/installed/x64-windows/include)
    endif()
    # Add additional Windows include paths if found
    # Use SYSTEM for musl to avoid glibc header conflicts (-isystem vs -I)
    if(ZLIB_INCLUDE_DIRS)
        include_directories(SYSTEM ${ZLIB_INCLUDE_DIRS})
    endif()
    if(LIBSODIUM_INCLUDE_DIRS)
        include_directories(SYSTEM ${LIBSODIUM_INCLUDE_DIRS})
    endif()
else()
    # Use pkg-config flags (matches Makefile CFLAGS approach)
    if(ZLIB_CFLAGS_OTHER)
        add_compile_options(${ZLIB_CFLAGS_OTHER})
    endif()
    if(LIBSODIUM_CFLAGS_OTHER)
        add_compile_options(${LIBSODIUM_CFLAGS_OTHER})
    endif()
    if(PORTAUDIO_CFLAGS_OTHER)
        add_compile_options(${PORTAUDIO_CFLAGS_OTHER})
    endif()

    # Don't add system include paths when using musl - musl-gcc handles this via -specs
    if(NOT USE_MUSL)
        include_directories(
            ${ZLIB_INCLUDE_DIRS}
            ${LIBSODIUM_INCLUDE_DIRS}
            ${PORTAUDIO_INCLUDE_DIRS}
        )
    else()
        # When using musl, strip /usr/include from all include paths and add the cleaned paths
        list(REMOVE_ITEM ZLIB_INCLUDE_DIRS "/usr/include")
        list(REMOVE_ITEM LIBSODIUM_INCLUDE_DIRS "/usr/include")
        list(REMOVE_ITEM PORTAUDIO_INCLUDE_DIRS "/usr/include")
        list(REMOVE_ITEM BEARSSL_INCLUDE_DIRS "/usr/include")

        # Add the musl-built library include paths (after removing /usr/include)
        include_directories(
            ${ZLIB_INCLUDE_DIRS}
            ${LIBSODIUM_INCLUDE_DIRS}
            ${PORTAUDIO_INCLUDE_DIRS}
            ${BEARSSL_INCLUDE_DIRS}
        )
    endif()
endif()

# =============================================================================
# Modular Library Architecture
# =============================================================================
#
# ASCII-Chat is organized into focused, independent modules for:
# - Faster incremental builds (only rebuild changed modules)
# - Better code organization (clear module boundaries)
# - Easier testing (link only what you need)
# - Optional unified library for external projects
#
# Module dependency tree:
#   ascii-chat-util       (no dependencies - foundation)
#   ascii-chat-platform   (depends on: util)
#   ascii-chat-crypto     (depends on: util, platform)
#   ascii-chat-simd       (depends on: util)
#   ascii-chat-video      (depends on: util, platform, simd)
#   ascii-chat-audio      (depends on: util, platform)
#   ascii-chat-network    (depends on: util, platform, crypto)
#   ascii-chat-core       (depends on: all above)
#
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
    lib/crypto/gpg.c
    lib/crypto/ssh_agent.c
    lib/crypto/keys/ssh_keys.c
    lib/crypto/keys/gpg_keys.c
    lib/crypto/keys/https_keys.c
    lib/crypto/keys/validation.c
)

# =============================================================================
# Module 3: Platform Abstraction (stable - changes monthly)
# =============================================================================
set(PLATFORM_SRCS_COMMON
    lib/platform/abstraction.c
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
        lib/os/windows/webcam_mediafoundation.c
    )

    # Add getopt for Windows (now in platform directory)
    if(EXISTS "${CMAKE_SOURCE_DIR}/lib/platform/windows/getopt.c")
        list(APPEND PLATFORM_SRCS lib/platform/windows/getopt.c)
    endif()
else()
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
    )

    if(PLATFORM_DARWIN)
        list(APPEND PLATFORM_SRCS
            lib/os/macos/webcam_avfoundation.m
        )
    else()
        list(APPEND PLATFORM_SRCS
            lib/os/linux/webcam_v4l2.c
        )
    endif()
endif()

# SIMD sources (architecture-specific, matching Makefile logic)
set(SIMD_SRCS)

# Always include common SIMD files (matches Makefile)
list(APPEND SIMD_SRCS
    lib/image2ascii/simd/ascii_simd.c
    lib/image2ascii/simd/ascii_simd_color.c
    lib/image2ascii/simd/common.c
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
    # Set specific compile flags for AVX2 files
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
# Module 4: SIMD (performance-critical - changes weekly)
# =============================================================================
# (Already defined above at line 1394)

# =============================================================================
# Module 5: Video Processing (changes weekly)
# =============================================================================
set(VIDEO_SRCS
    lib/image2ascii/image.c
    lib/image2ascii/output_buffer.c
    lib/image2ascii/ascii.c
    lib/ansi_fast.c
    lib/palette.c
    lib/utf8.c
    lib/video_frame.c
    lib/os/webcam.c
)

# Platform-specific webcam sources already added to PLATFORM_SRCS
# (webcam_avfoundation.m, webcam_v4l2.c, webcam_mediafoundation.c)

# =============================================================================
# Module 6: Audio Processing (changes weekly)
# =============================================================================
set(AUDIO_SRCS
    lib/audio.c
    lib/mixer.c
    lib/ringbuffer.c
)

# =============================================================================
# Module 7: Network (changes weekly)
# =============================================================================
set(NETWORK_SRCS
    lib/network/network.c
    lib/network/packet.c
    lib/network/av.c
    lib/packet_queue.c
    lib/buffer_pool.c
    lib/hashtable.c
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
    lib/lock_debug.c
    lib/version.c
)

# =============================================================================
# Create Modular Libraries
# =============================================================================

# Helper macro to create a module with common settings
macro(create_ascii_chat_module MODULE_NAME MODULE_SRCS)
    add_library(${MODULE_NAME} STATIC ${MODULE_SRCS})

    # Version dependency
    add_dependencies(${MODULE_NAME} generate_version)

    # Include paths
    target_include_directories(${MODULE_NAME} PUBLIC ${CMAKE_BINARY_DIR}/generated)

    # Project source root for logging
    target_compile_definitions(${MODULE_NAME} PRIVATE PROJECT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

    # MI_DEBUG for mimalloc
    if(DEFINED MIMALLOC_DEBUG_LEVEL)
        target_compile_definitions(${MODULE_NAME} PRIVATE MI_DEBUG=${MIMALLOC_DEBUG_LEVEL})
    endif()

    # Musl flag
    if(USE_MUSL)
        target_compile_definitions(${MODULE_NAME} PRIVATE USE_MUSL=1)
    endif()
endmacro()

# -----------------------------------------------------------------------------
# Module 1: Utilities (no dependencies)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-util "${UTIL_SRCS}")

# -----------------------------------------------------------------------------
# Module 2: Platform Abstraction (depends on: util)
# -----------------------------------------------------------------------------

create_ascii_chat_module(ascii-chat-platform "${PLATFORM_SRCS}")
target_link_libraries(ascii-chat-platform ascii-chat-util)

# Add kernel headers for musl builds (needed for V4L2)
if(USE_MUSL AND EXISTS "${FETCHCONTENT_BASE_DIR}/musl-deps/kernel-headers")
    target_include_directories(ascii-chat-platform PRIVATE
        "${FETCHCONTENT_BASE_DIR}/musl-deps/kernel-headers"
    )
endif()

# Platform-specific system libraries
if(WIN32)
    target_link_libraries(ascii-chat-platform
        ${WS2_32_LIB}
        ${USER32_LIB}
        ${ADVAPI32_LIB}
        ${DBGHELP_LIB}
        ${MF_LIB}
        ${MFPLAT_LIB}
        ${MFREADWRITE_LIB}
        ${MFUUID_LIB}
        ${OLE32_LIB}
        crypt32  # For Windows crypto certificate functions
    )
else()
    if(PLATFORM_DARWIN)
        target_link_libraries(ascii-chat-platform
            ${FOUNDATION_FRAMEWORK}
            ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK}
            ${COREVIDEO_FRAMEWORK}
        )
    elseif(PLATFORM_LINUX)
        target_link_libraries(ascii-chat-platform ${CMAKE_THREAD_LIBS_INIT})
    endif()
endif()

# -----------------------------------------------------------------------------
# Module 3: Cryptography (depends on: util, platform)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-crypto "${CRYPTO_SRCS}")
target_link_libraries(ascii-chat-crypto
    ascii-chat-util
    ascii-chat-platform
    ${LIBSODIUM_LIBRARIES}
)

# Add libgcrypt if available
if(LIBGCRYPT_FOUND)
    target_link_libraries(ascii-chat-crypto ${LIBGCRYPT_LIBRARIES})
    target_include_directories(ascii-chat-crypto PRIVATE ${LIBGCRYPT_INCLUDE_DIRS})
    target_compile_definitions(ascii-chat-crypto PRIVATE HAVE_LIBGCRYPT)
endif()

# Add BearSSL if available
if(BEARSSL_FOUND)
    target_link_libraries(ascii-chat-crypto ${BEARSSL_LIBRARIES})
    target_include_directories(ascii-chat-crypto PRIVATE ${BEARSSL_INCLUDE_DIRS})
endif()

# -----------------------------------------------------------------------------
# Module 4: SIMD (depends on: util)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-simd "${SIMD_SRCS}")
target_link_libraries(ascii-chat-simd ascii-chat-util)

# -----------------------------------------------------------------------------
# Module 5: Video Processing (depends on: util, platform, simd)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-video "${VIDEO_SRCS}")
target_link_libraries(ascii-chat-video
    ascii-chat-util
    ascii-chat-platform
    ascii-chat-simd
)

# -----------------------------------------------------------------------------
# Module 6: Audio Processing (depends on: util, platform)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-audio "${AUDIO_SRCS}")
target_link_libraries(ascii-chat-audio
    ascii-chat-util
    ascii-chat-platform
    ${PORTAUDIO_LIBRARIES}
)

# JACK support for Linux (optional)
if(NOT WIN32 AND PLATFORM_LINUX AND JACK_FOUND AND NOT USE_MUSL)
    target_link_libraries(ascii-chat-audio ${JACK_LIBRARIES})
endif()

# -----------------------------------------------------------------------------
# Module 7: Network (depends on: util, platform, crypto)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-network "${NETWORK_SRCS}")
target_link_libraries(ascii-chat-network
    ascii-chat-util
    ascii-chat-platform
    ascii-chat-crypto
    ${ZLIB_LIBRARIES}
)

# -----------------------------------------------------------------------------
# Module 8: Core Application (depends on: all above)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-core "${CORE_SRCS}")
target_link_libraries(ascii-chat-core
    ascii-chat-util
    ascii-chat-platform
    ascii-chat-crypto
    ascii-chat-simd
    ascii-chat-video
    ascii-chat-audio
    ascii-chat-network
    ${ZLIB_LIBRARIES}
)

# Math library (Unix only - Windows has math functions in C runtime)
if(NOT WIN32)
    target_link_libraries(ascii-chat-core m)
endif()

# Special musl handling for libexecinfo
if(USE_MUSL)
    target_link_libraries(ascii-chat-core ${MUSL_PREFIX}/lib/libexecinfo.a)
    add_dependencies(ascii-chat-core libexecinfo-musl)
endif()

# mimalloc for core
if(USE_MIMALLOC)
    target_link_libraries(ascii-chat-core ${MIMALLOC_LIBRARIES})
endif()

# =============================================================================
# Unified Library Targets (OPTIONAL - not built by default)
# =============================================================================
# These combine all modules into a single library for external projects.
# Build with: cmake --build build --target ascii-chat-static
#         or: cmake --build build --target ascii-chat-shared
# =============================================================================

# Shared unified library (libasciichat.so / libasciichat.dylib / asciichat.dll)
# Links all module static libraries together without recompiling source code
# macOS note: We use -Wl,-all_load to force inclusion of all symbols from static libs
# Linux note: We use -Wl,--whole-archive to force inclusion of all symbols
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/empty.c "// Empty file for shared library\n")
add_library(ascii-chat-shared SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_BINARY_DIR}/empty.c)
set_target_properties(ascii-chat-shared PROPERTIES OUTPUT_NAME "asciichat")

# Link all module libraries
if(APPLE)
    target_link_libraries(ascii-chat-shared PRIVATE
        -Wl,-all_load
        ascii-chat-util ascii-chat-platform ascii-chat-crypto ascii-chat-simd
        ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
    )
else()
    target_link_libraries(ascii-chat-shared PRIVATE
        -Wl,--whole-archive
        ascii-chat-util ascii-chat-platform ascii-chat-crypto ascii-chat-simd
        ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
        -Wl,--no-whole-archive
    )
endif()

# Add system library dependencies
if(WIN32)
    target_link_libraries(ascii-chat-shared PRIVATE
        ${WS2_32_LIB} ${USER32_LIB} ${ADVAPI32_LIB} ${DBGHELP_LIB}
        ${MF_LIB} ${MFPLAT_LIB} ${MFREADWRITE_LIB} ${MFUUID_LIB} ${OLE32_LIB}
        ${PORTAUDIO_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBSODIUM_LIBRARIES}
    )
    if(BEARSSL_FOUND)
        target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
    endif()
    if(LIBGCRYPT_FOUND)
        target_link_libraries(ascii-chat-shared PRIVATE ${LIBGCRYPT_LIBRARIES})
    endif()
    if(USE_MIMALLOC)
        target_link_libraries(ascii-chat-shared PRIVATE ${MIMALLOC_LIBRARIES})
    endif()
else()
    target_link_libraries(ascii-chat-shared PRIVATE
        ${PORTAUDIO_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBSODIUM_LIBRARIES} m
    )
    if(BEARSSL_FOUND)
        target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
    endif()
    if(LIBGCRYPT_FOUND)
        target_link_libraries(ascii-chat-shared PRIVATE ${LIBGCRYPT_LIBRARIES})
    endif()
    if(PLATFORM_DARWIN)
        target_link_libraries(ascii-chat-shared PRIVATE
            ${FOUNDATION_FRAMEWORK} ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK} ${COREVIDEO_FRAMEWORK}
        )
    elseif(PLATFORM_LINUX)
        target_link_libraries(ascii-chat-shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
        if(JACK_FOUND AND NOT USE_MUSL)
            target_link_libraries(ascii-chat-shared PRIVATE ${JACK_LIBRARIES})
        endif()
    endif()
    if(USE_MUSL)
        target_link_libraries(ascii-chat-shared PRIVATE ${MUSL_PREFIX}/lib/libexecinfo.a)
        add_dependencies(ascii-chat-shared libexecinfo-musl)
    endif()
    if(USE_MIMALLOC)
        target_link_libraries(ascii-chat-shared PRIVATE ${MIMALLOC_LIBRARIES})
    endif()
endif()

# Static unified library (libasciichat.a)
# Extracts object files from module .a files and combines them without recompiling
# macOS uses libtool -static, Linux uses ar MRI script
if(APPLE)
    # macOS: Use libtool to combine static libraries
    add_custom_target(ascii-chat-static
        COMMAND libtool -static -o ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a
            $<TARGET_FILE:ascii-chat-util>
            $<TARGET_FILE:ascii-chat-platform>
            $<TARGET_FILE:ascii-chat-crypto>
            $<TARGET_FILE:ascii-chat-simd>
            $<TARGET_FILE:ascii-chat-video>
            $<TARGET_FILE:ascii-chat-audio>
            $<TARGET_FILE:ascii-chat-network>
            $<TARGET_FILE:ascii-chat-core>
        DEPENDS
            ascii-chat-util ascii-chat-platform ascii-chat-crypto ascii-chat-simd
            ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
        COMMENT "Combining module libraries into libasciichat.a (no recompilation)"
        COMMAND_EXPAND_LISTS
    )
else()
    # Linux/Windows: Use ar MRI script to combine archives
    add_custom_target(ascii-chat-static
        COMMAND ${CMAKE_COMMAND} -E echo "CREATE ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a" > ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-util>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-platform>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-crypto>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-simd>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-video>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-audio>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-network>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-core>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "SAVE" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "END" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_AR} -M < ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        DEPENDS
            ascii-chat-util ascii-chat-platform ascii-chat-crypto ascii-chat-simd
            ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
        COMMENT "Combining module libraries into libasciichat.a (no recompilation)"
        COMMAND_EXPAND_LISTS
    )
endif()

# Print helpful message about optional targets
# =============================================================================
# Library Alias for Tests
# =============================================================================
# Create an alias that links all modules for test compatibility
# This allows tests to link against ascii-chat-lib instead of individual modules
add_library(ascii-chat-lib INTERFACE)
target_link_libraries(ascii-chat-lib INTERFACE
    ascii-chat-util
    ascii-chat-platform
    ascii-chat-crypto
    ascii-chat-simd
    ascii-chat-video
    ascii-chat-audio
    ascii-chat-network
    ascii-chat-core
)

message(STATUS "")
message(STATUS "=== Modular Library Architecture ===")
message(STATUS "Individual modules: ascii-chat-util, ascii-chat-platform, ascii-chat-crypto,")
message(STATUS "                    ascii-chat-simd, ascii-chat-video, ascii-chat-audio,")
message(STATUS "                    ascii-chat-network, ascii-chat-core")
message(STATUS "")
message(STATUS "Optional unified libraries (not built by default):")
message(STATUS "  Static:  cmake --build build --target ascii-chat-static  → libasciichat.a")
message(STATUS "  Shared:  cmake --build build --target ascii-chat-shared  → libasciichat.so/.dylib/.dll")
message(STATUS "")

# =============================================================================
# Precompiled Headers (Speed up compilation by 20-30%)
# =============================================================================

# CMake 3.16+ supports target_precompile_headers
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    option(USE_PRECOMPILED_HEADERS "Use precompiled headers for faster builds" ON)

    if(USE_PRECOMPILED_HEADERS)
        # Add most commonly included headers as precompiled for the core module
        # Based on analysis: these headers appear in 30+ source files
        #
        # NOTE: We CANNOT use precompiled headers with common.h because it defines
        # malloc/free macros that conflict with system headers. When a PCH includes
        # common.h, every file using the PCH tries to redefine malloc/free when
        # system headers like <malloc.h> are included, causing compiler errors.
        #
        # Instead, we precompile only safe headers that don't have macro conflicts.
        target_precompile_headers(ascii-chat-core PRIVATE
            # Platform abstraction (safe - no conflicting macros)
            lib/platform/abstraction.h

            # Standard C headers (most frequently used, safe from macro conflicts)
            <stdio.h>
            <string.h>
            <stdbool.h>
            <stdint.h>
            <stddef.h>
            <stdatomic.h>
            <time.h>
            <errno.h>
        )
        message(STATUS "${BoldGreen}Precompiled headers enabled (excluding common.h due to macro conflicts)${ColorReset}")
    endif()
else()
    message(STATUS "Precompiled headers require CMake 3.16+ (you have ${CMAKE_VERSION})")
endif()

# =============================================================================
# Executables
# =============================================================================

# Unified binary with both server and client modes
add_executable(ascii-chat
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
)

# Enable dead code elimination for optimal binary size
target_compile_options(ascii-chat PRIVATE -ffunction-sections -fdata-sections)
if(NOT WIN32)
    if(APPLE)
        target_link_options(ascii-chat PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(ascii-chat PRIVATE -Wl,--gc-sections)
    endif()
endif()

target_link_libraries(ascii-chat ascii-chat-core)

# Preserve custom .ascii_chat_version section during linking
if(NOT WIN32)
    # Unix/macOS: Use linker flags to keep custom section
    target_link_options(ascii-chat PRIVATE
        "LINKER:--undefined=ascii_chat_version_string"
        "LINKER:--undefined=ascii_chat_comment_string"
    )
endif()

# Disable PIE for Debug/Dev builds so addr2line can resolve backtrace addresses
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(ascii-chat PRIVATE -no-pie)
endif()

# Strip symbols in Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    find_program(STRIP_EXECUTABLE strip)
    if(STRIP_EXECUTABLE)
        add_custom_command(TARGET ascii-chat POST_BUILD
            COMMAND ${STRIP_EXECUTABLE} $<TARGET_FILE:ascii-chat>
            COMMENT "Stripping symbols from ascii-chat"
        )
    endif()
endif()

# Add musl dependency if building with musl
if(USE_MUSL)
    # Add dependencies on all musl libraries (they'll build automatically)
    add_dependencies(ascii-chat portaudio-musl alsa-lib-musl libsodium-musl zlib-musl libexecinfo-musl)

    # Link against musl-built static libraries
    target_link_directories(ascii-chat PRIVATE ${MUSL_PREFIX}/lib)
    target_link_options(ascii-chat PRIVATE -static -rdynamic)

    # Link all libraries (LTO + dead code elimination removes unused code)
    target_link_libraries(ascii-chat
        ${MUSL_PREFIX}/lib/libportaudio.a
        ${MUSL_PREFIX}/lib/libasound.a
        ${MUSL_PREFIX}/lib/libsodium.a
        ${MUSL_PREFIX}/lib/libz.a
        ${MUSL_PREFIX}/lib/libexecinfo.a
        -lm -lpthread
    )
endif()

# macOS Info.plist embedding (for client mode webcam access)
if(PLATFORM_DARWIN AND EXISTS "${CMAKE_SOURCE_DIR}/Info.plist")
    set_target_properties(ascii-chat PROPERTIES
        LINK_FLAGS "-sectcreate __TEXT __info_plist ${CMAKE_SOURCE_DIR}/Info.plist"
    )
endif()

# =============================================================================
# No Backwards Compatibility - Single Binary Only
# =============================================================================
# The unified binary is invoked as:
#   ./ascii-chat server [options...]
#   ./ascii-chat client [options...]

# =============================================================================
# Tests (matching Makefile test infrastructure)
# =============================================================================

# Test framework setup
if(BUILD_TESTS AND CRITERION_FOUND)
    enable_testing()

    # Build test LDFLAGS systematically (matches Makefile complex setup)
    set(TEST_LDFLAGS ${CRITERION_LIBRARIES})
    # Criterion requires libffi for theories support
    list(APPEND TEST_LDFLAGS ffi)

    if(PLATFORM_LINUX)
        # Add Linux-specific test dependencies (matching Makefile order)
        if(PROTOBUF_C_FOUND)
            list(APPEND TEST_LDFLAGS ${PROTOBUF_C_LIBRARIES})
        elseif(PROTOBUF_C_LIBRARIES)
            list(APPEND TEST_LDFLAGS ${PROTOBUF_C_LIBRARIES})
        else()
            # Protobuf-C is optional - only add if library exists
            find_library(PROTOBUF_C_LIB protobuf-c)
            if(PROTOBUF_C_LIB)
                list(APPEND TEST_LDFLAGS protobuf-c)
            endif()
        endif()

        if(NANOPB_FOUND)
            list(APPEND TEST_LDFLAGS ${NANOPB_LIBRARIES})
        elseif(NANOPB_LIBRARIES)
            list(APPEND TEST_LDFLAGS ${NANOPB_LIBRARIES})
        elseif(EXISTS /usr/lib/x86_64-linux-gnu/libprotobuf-nanopb.a)
            list(APPEND TEST_LDFLAGS /usr/lib/x86_64-linux-gnu/libprotobuf-nanopb.a)
        endif()

        # Boxfort (subprocess isolation) - skip for musl builds (requires glibc)
        if(NOT USE_MUSL)
            if(BOXFORT_FOUND)
                list(APPEND TEST_LDFLAGS ${BOXFORT_LIBRARIES})
            elseif(BOXFORT_LIBRARIES)
                list(APPEND TEST_LDFLAGS ${BOXFORT_LIBRARIES})
            else()
                # Boxfort is optional - only add if library exists
                find_library(BOXFORT_LIB boxfort)
                if(BOXFORT_LIB)
                    list(APPEND TEST_LDFLAGS boxfort)
                endif()
            endif()
        endif()

        # Optional dependencies (skip for musl static builds to avoid dynamic library issues)
        if(NOT USE_MUSL)
            if(NANOMSG_FOUND)
                list(APPEND TEST_LDFLAGS ${NANOMSG_LIBRARIES})
            endif()
            if(LIBGIT2_FOUND)
                list(APPEND TEST_LDFLAGS ${LIBGIT2_LIBRARIES})

                # libgit2 requires OpenSSL, libssh2, and http_parser when statically linked
                find_package(OpenSSL)
                if(OpenSSL_FOUND)
                    list(APPEND TEST_LDFLAGS OpenSSL::SSL OpenSSL::Crypto)
                else()
                    # Fallback to direct library names
                    list(APPEND TEST_LDFLAGS ssl crypto)
                endif()

            # Additional libgit2 dependencies (required for static linking)
            # These must be linked unconditionally when using static libgit2
            # Check if each library exists before adding

            # On Linux, provide search hints for standard locations
            if(PLATFORM_LINUX)
                find_library(SSH2_LIB ssh2 HINTS /usr/lib/x86_64-linux-gnu /usr/lib /lib)
                find_library(HTTP_PARSER_LIB http_parser HINTS /usr/lib/x86_64-linux-gnu /usr/lib /lib)
                find_library(PCRE2_LIB pcre2-8 HINTS /usr/lib/x86_64-linux-gnu /usr/lib /lib)
            else()
                find_library(SSH2_LIB ssh2)
                find_library(HTTP_PARSER_LIB http_parser)
                find_library(PCRE2_LIB pcre2-8)
            endif()

            # Only link libgit2 dependencies if they exist (make them optional)
            if(SSH2_LIB)
                list(APPEND TEST_LDFLAGS ssh2)
                message(STATUS "Added libssh2 for tests")
            else()
                message(WARNING "libssh2 not found - some git features may not work")
            endif()

            if(HTTP_PARSER_LIB)
                list(APPEND TEST_LDFLAGS http_parser)
                message(STATUS "Added http_parser for tests")
            else()
                message(WARNING "http_parser not found - some git features may not work")
            endif()

            if(PCRE2_LIB)
                list(APPEND TEST_LDFLAGS pcre2-8)
                message(STATUS "Added pcre2-8 for tests")
            else()
                message(WARNING "pcre2-8 not found - some git features may not work")
            endif()

            if(NOT SSH2_LIB AND NOT HTTP_PARSER_LIB AND NOT PCRE2_LIB)
                message(WARNING "No libgit2 dependencies found - git features will be disabled")
            endif()
            endif() # LIBGIT2_FOUND
        endif() # NOT USE_MUSL

        # GSSAPI/Kerberos support (skip for musl to avoid dynamic library issues)
        if(NOT USE_MUSL)
            if(KRB5_GSSAPI_FOUND)
                list(APPEND TEST_LDFLAGS ${KRB5_GSSAPI_LIBRARIES})
            elseif(LIBSSH2_FOUND)
                list(APPEND TEST_LDFLAGS ${LIBSSH2_LIBRARIES})
            else()
                list(APPEND TEST_LDFLAGS gssapi_krb5 krb5 k5crypto com_err)
            endif()
        endif()

        # Additional system libraries (matches Makefile)
        # Make these optional - only link if available
        # Note: ssh2, http_parser, and pcre2-8 are already handled above with libgit2
        foreach(lib dl resolv)
            find_library(${lib}_LIB ${lib})
            if(${lib}_LIB)
                list(APPEND TEST_LDFLAGS ${lib})
            endif()
        endforeach()
    elseif(PLATFORM_DARWIN)
        # macOS test linking (simpler, matches Makefile)
        if(NOT CRITERION_LIBRARIES)
            # Fallback for Homebrew
            list(APPEND TEST_LDFLAGS "-L/opt/homebrew/lib" criterion)
        endif()
    endif()

    # Find test files (excluding problematic ones, matches Makefile)
    file(GLOB_RECURSE TEST_SRCS_ALL tests/unit/*.c tests/integration/*.c tests/performance/*.c)
    set(TEST_EXCLUDES
        tests/integration/server_multiclient_test.c
        tests/integration/video_pipeline_test.c
    )

    set(TEST_SRCS)
    foreach(test_src IN LISTS TEST_SRCS_ALL)
        list(FIND TEST_EXCLUDES ${test_src} IS_EXCLUDED)
        if(IS_EXCLUDED EQUAL -1)
            list(APPEND TEST_SRCS ${test_src})
        endif()
    endforeach()

    # Create test executables (matches Makefile naming convention)
    set(ALL_TEST_TARGETS)
    foreach(test_src ${TEST_SRCS})
        # Transform test file paths to executable names with flattened structure
        # tests/unit/common_test.c -> test_unit_common
        # tests/integration/crypto_network_test.c -> test_integration_crypto_network
        get_filename_component(test_name ${test_src} NAME_WE)
        get_filename_component(test_dir ${test_src} DIRECTORY)
        get_filename_component(test_subdir ${test_dir} NAME)

        string(REPLACE "_test" "" test_base ${test_name})
        set(test_exe_name "test_${test_subdir}_${test_base}")
        list(APPEND ALL_TEST_TARGETS ${test_exe_name})

        # Add test executable with test utilities (EXCLUDE_FROM_ALL = not built by default)
        # - globals.c: Provides global symbols (g_should_exit) needed by lib code
        # - logging.c: Provides test-specific logging utilities (stdout/stderr redirection)
        add_executable(${test_exe_name} EXCLUDE_FROM_ALL ${test_src} lib/tests/globals.c lib/tests/logging.c)

        # Disable precompiled headers for test targets to avoid conflicts with Criterion macros
        set_target_properties(${test_exe_name} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

        # Add Criterion include directories
        target_include_directories(${test_exe_name} PRIVATE ${CRITERION_INCLUDE_DIRS})

        # For musl builds, disable subprocess isolation (no forking)
        if(USE_MUSL)
            target_compile_definitions(${test_exe_name} PRIVATE CRITERION_NO_EARLY_EXIT=1)
        endif()

        # Link test dependencies (order matters for linking)
        target_link_libraries(${test_exe_name}
            ascii-chat-lib
            ${TEST_LDFLAGS}
        )

        # For musl static builds, allow undefined boxfort references (they won't be called)
        if(USE_MUSL)
            target_link_options(${test_exe_name} PRIVATE
                -Wl,--allow-shlib-undefined
                -Wl,--unresolved-symbols=ignore-all
            )
        endif()

        # Use release objects for performance tests
        if(test_subdir STREQUAL "performance")
            target_compile_options(${test_exe_name} PRIVATE -O3 -DNDEBUG)
        endif()

        # Add to CTest
        add_test(NAME ${test_exe_name} COMMAND ${test_exe_name})
    endforeach()

    # Target to build all tests without running them (excluded from default build)
    add_custom_target(tests
        DEPENDS ${ALL_TEST_TARGETS}
        COMMENT "Building all test executables"
    )

    # Custom targets for different test modes (matches Makefile)
    add_custom_target(test_debug
        DEPENDS tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running tests in debug mode"
    )

    add_custom_target(test_release
        DEPENDS tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C Release
        COMMENT "Running tests in release mode"
    )

    # Overall test target
    add_custom_target(test_all
        DEPENDS tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running all tests"
    )

else()
    if(USE_MUSL AND BUILD_TESTS)
        message(STATUS "Tests disabled: System Criterion requires glibc (boxfort dependency)")
        message(STATUS "  -> Use glibc build for tests: cmake -B build -DBUILD_TESTS=ON")
        message(STATUS "  -> Musl builds are for deployment, not testing")
    elseif(BUILD_TESTS)
        message(WARNING "Criterion testing framework not found. Tests will not be built.")
    endif()
endif()

# =============================================================================
# Installation
# =============================================================================

install(TARGETS ascii-chat
    RUNTIME DESTINATION bin
)

# =============================================================================
# Custom Targets (matching Makefile utilities)
# =============================================================================

# Format target (matches Makefile format target)

# Format target - cross-platform version
file(GLOB_RECURSE ALL_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/**/*.c
    ${CMAKE_SOURCE_DIR}/src/**/*.h
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/lib/**/*.c
    ${CMAKE_SOURCE_DIR}/lib/**/*.h
    ${CMAKE_SOURCE_DIR}/lib/*.c
    ${CMAKE_SOURCE_DIR}/lib/*.h
    ${CMAKE_SOURCE_DIR}/tests/**/*.c
    ${CMAKE_SOURCE_DIR}/tests/**/*.h
    ${CMAKE_SOURCE_DIR}/tests/*.c
    ${CMAKE_SOURCE_DIR}/tests/*.h
)

# Include Objective-C files on Apple platforms
if(APPLE)
    file(GLOB_RECURSE OBJC_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/lib/**/*.m
        ${CMAKE_SOURCE_DIR}/lib/*.m
        ${CMAKE_SOURCE_DIR}/src/**/*.m
        ${CMAKE_SOURCE_DIR}/src/*.m
    )
    list(APPEND ALL_SOURCE_FILES ${OBJC_SOURCE_FILES})
endif()

# Create production source files list (exclude tests for clang-tidy)
file(GLOB_RECURSE PRODUCTION_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/**/*.c
    ${CMAKE_SOURCE_DIR}/src/**/*.h
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/lib/**/*.c
    ${CMAKE_SOURCE_DIR}/lib/**/*.h
    ${CMAKE_SOURCE_DIR}/lib/*.c
    ${CMAKE_SOURCE_DIR}/lib/*.h
)

# Include Objective-C files in production source files on Apple platforms
if(APPLE)
    file(GLOB_RECURSE OBJC_PRODUCTION_FILES
        ${CMAKE_SOURCE_DIR}/lib/**/*.m
        ${CMAKE_SOURCE_DIR}/lib/*.m
        ${CMAKE_SOURCE_DIR}/src/**/*.m
        ${CMAKE_SOURCE_DIR}/src/*.m
    )
    list(APPEND PRODUCTION_SOURCE_FILES ${OBJC_PRODUCTION_FILES})
endif()

# Filter out third-party or generated files if needed
# Note: Objective-C files (.m) are included for Apple platforms

# Find clang-format executable
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)

if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${ALL_SOURCE_FILES}
        COMMENT "Formatting source code with clang-format"
        VERBATIM
    )

    # Alias target: clang-format (same as format)
    add_custom_target(clang-format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${ALL_SOURCE_FILES}
        COMMENT "Formatting source code with clang-format"
        VERBATIM
    )

    # Format check target
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run -Werror ${ALL_SOURCE_FILES}
        COMMENT "Checking code formatting"
        VERBATIM
    )
else()
    message(WARNING "clang-format not found. Format targets will not be available.")
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found. Please install clang-format."
    )
    add_custom_target(clang-format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found. Please install clang-format."
    )
    add_custom_target(format-check
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found. Please install clang-format."
    )
endif()

# Find clang-tidy executable
find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)

if(CLANG_TIDY_EXECUTABLE)
    add_custom_target(clang-tidy
        COMMAND ${CLANG_TIDY_EXECUTABLE}
            --config-file ${CMAKE_SOURCE_DIR}/.clang-tidy
            -p ${CMAKE_BINARY_DIR}
            ${PRODUCTION_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-tidy static analysis on production code (excluding tests)"
        VERBATIM
    )
else()
    message(WARNING "clang-tidy not found. Clang-tidy target will not be available.")
    add_custom_target(clang-tidy
        COMMAND ${CMAKE_COMMAND} -E echo "clang-tidy not found. Please install clang-tidy."
    )
endif()

# Find scan-build executable
find_program(SCAN_BUILD_EXECUTABLE NAMES scan-build)

if(SCAN_BUILD_EXECUTABLE)
    add_custom_target(scan-build
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/scan-build-results
        COMMAND ${SCAN_BUILD_EXECUTABLE}
            --use-analyzer=${CMAKE_C_COMPILER}
            -o ${CMAKE_BINARY_DIR}/scan-build-results
            ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ascii-chat
        COMMENT "Running scan-build static analyzer"
        VERBATIM
    )

    add_custom_target(scan-build-view
        COMMAND ${CMAKE_COMMAND} -E echo "Opening scan-build results in browser..."
        COMMAND open ${CMAKE_BINARY_DIR}/scan-build-results/*/index.html || xdg-open ${CMAKE_BINARY_DIR}/scan-build-results/*/index.html || start ${CMAKE_BINARY_DIR}/scan-build-results/*/index.html
        DEPENDS scan-build
        COMMENT "Opening scan-build results"
        VERBATIM
    )
else()
    message(WARNING "scan-build not found. Scan-build targets will not be available.")
    add_custom_target(scan-build
        COMMAND ${CMAKE_COMMAND} -E echo "scan-build not found. Please install clang static analyzer."
    )
endif()

# Clean all target (matches Makefile clean)
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/lib
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
    COMMENT "Cleaning all build artifacts"
)

# Dependency installation targets (matches Makefile deps targets)
if(PLATFORM_DARWIN)
    add_custom_target(deps
        COMMAND brew install portaudio libsodium criterion zlib coreutils make || true
        COMMENT "Installing macOS dependencies via Homebrew"
    )
    add_custom_target(deps-test
        COMMAND brew install gcovr || true
        COMMENT "Installing macOS test dependencies"
        DEPENDS deps
    )
elseif(PLATFORM_LINUX)
    add_custom_target(deps
        COMMAND sudo apt-get update && sudo apt-get install -y build-essential pkg-config libportaudio2 portaudio19-dev libsodium-dev libcriterion-dev zlib1g-dev || true
        COMMENT "Installing Linux dependencies via apt"
    )
    add_custom_target(deps-test
        COMMAND sudo apt-get install -y lcov gcovr valgrind || true
        COMMENT "Installing Linux test dependencies"
        DEPENDS deps
    )
endif()

# =============================================================================
# Status Messages
# =============================================================================

# Status Messages (matching Makefile help output)
message(STATUS "")
message(STATUS "========================================")
message(STATUS "ASCII-Chat Configuration:")
message(STATUS "========================================")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Standard:        C${CMAKE_C_STANDARD}")
message(STATUS "  C Compiler:        ${CMAKE_C_COMPILER}")
if(CMAKE_C_COMPILER_LAUNCHER)
    message(STATUS "  Compiler Launcher: ${CMAKE_C_COMPILER_LAUNCHER}")
endif()
message(STATUS "  Compiler ID:       ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Archiver:          ${CMAKE_AR}")
message(STATUS "  Ranlib:            ${CMAKE_RANLIB}")
message(STATUS "  Platform:          ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Processor:         ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  CPU Cores:         ${CPU_CORES}")

# Determine build tool information
message(STATUS "  Build Tool:        ${CMAKE_GENERATOR}")
if(CMAKE_GENERATOR STREQUAL "Ninja")
    message(STATUS "  Build Binary:      ninja")
elseif(CMAKE_GENERATOR MATCHES "Unix Makefiles")
    if(CMAKE_MAKE_PROGRAM)
        get_filename_component(MAKE_BINARY "${CMAKE_MAKE_PROGRAM}" NAME)
        message(STATUS "  Build Binary:      ${MAKE_BINARY}")
    else()
        message(STATUS "  Build Binary:      make")
    endif()
elseif(CMAKE_GENERATOR MATCHES "Visual Studio")
    message(STATUS "  Build Binary:      msbuild")
elseif(CMAKE_GENERATOR MATCHES "Xcode")
    message(STATUS "  Build Binary:      xcodebuild")
else()
    message(STATUS "  Build Binary:      ${CMAKE_GENERATOR}")
endif()
if(WIN32)
    if(WINDOWS_SDK_VERSION)
        message(STATUS "  Windows SDK:       ${WINDOWS_SDK_VERSION}")
    endif()
    if(PLATFORM_WINDOWS_ARM64)
        message(STATUS "  Architecture:      ARM64")
    elseif(PLATFORM_WINDOWS_ARM)
        message(STATUS "  Architecture:      ARM")
    else()
        message(STATUS "  Architecture:      x64")
    endif()
elseif(PLATFORM_DARWIN)
    message(STATUS "  Apple Silicon:     ${IS_APPLE_SILICON}")
    message(STATUS "  Rosetta:           ${IS_ROSETTA}")
endif()

# Display environment configuration if set
if(DEFINED ENV{CC} OR DEFINED ENV{CFLAGS} OR DEFINED ENV{CPPFLAGS} OR DEFINED ENV{LDFLAGS})
    message(STATUS "")
    message(STATUS "Environment Configuration:")
    if(DEFINED ENV{CC})
        message(STATUS "  CC:                $ENV{CC}")
    endif()
    if(DEFINED ENV{CFLAGS})
        message(STATUS "  CFLAGS:            $ENV{CFLAGS}")
    endif()
    if(DEFINED ENV{CPPFLAGS})
        message(STATUS "  CPPFLAGS:          $ENV{CPPFLAGS}")
    endif()
    if(DEFINED ENV{LDFLAGS})
        message(STATUS "  LDFLAGS:           $ENV{LDFLAGS}")
    endif()
endif()

message(STATUS "")
message(STATUS "Hardware Acceleration:")
message(STATUS "  SIMD Mode:         ${SIMD_MODE}")
if(ENABLE_SIMD_SSE2)
    message(STATUS "  SSE2:              ${Green}Enabled${ColorReset}")
else()
    message(STATUS "  SSE2:              ${Red}Disabled${ColorReset}")
endif()
if(ENABLE_SIMD_SSSE3)
    message(STATUS "  SSSE3:             ${Green}Enabled${ColorReset}")
else()
    message(STATUS "  SSSE3:             ${Red}Disabled${ColorReset}")
endif()
if(ENABLE_SIMD_AVX2)
    message(STATUS "  AVX2:              ${Green}Enabled${ColorReset}")
else()
    message(STATUS "  AVX2:              ${Red}Disabled${ColorReset}")
endif()
if(ENABLE_SIMD_NEON)
    message(STATUS "  NEON:              ${Green}Enabled${ColorReset}")
else()
    message(STATUS "  NEON:              ${Red}Disabled${ColorReset}")
endif()
if(ENABLE_SIMD_SVE)
    message(STATUS "  SVE:               ${Green}Enabled${ColorReset}")
else()
    message(STATUS "  SVE:               ${Red}Disabled${ColorReset}")
endif()
if(ENABLE_CRC32_HW)
    message(STATUS "  CRC32 HW:          ${Green}Enabled${ColorReset}")
else()
    message(STATUS "  CRC32 HW:          ${Red}Disabled${ColorReset}")
endif()
message(STATUS "")
message(STATUS "Dependencies:")
if(CRITERION_FOUND)
    message(STATUS "  Tests:             ${Green}Found${ColorReset}")
else()
    message(STATUS "  Tests:             ${Red}Not Found${ColorReset}")
endif()
if(ZLIB_FOUND)
    message(STATUS "  zlib:              ${Green}Found${ColorReset}")
else()
    message(STATUS "  zlib:              ${Red}Not Found${ColorReset}")
endif()
if(LIBSODIUM_FOUND)
    message(STATUS "  libsodium:         ${Green}Found${ColorReset}")
else()
    message(STATUS "  libsodium:         ${Red}Not Found${ColorReset}")
endif()
if(BEARSSL_FOUND)
    message(STATUS "  BearSSL:           ${Green}Found${ColorReset}")
else()
    message(STATUS "  BearSSL:           ${Red}Not Found${ColorReset}")
endif()
if(PORTAUDIO_FOUND)
    message(STATUS "  PortAudio:         ${Green}Found${ColorReset}")
else()
    message(STATUS "  PortAudio:         ${Red}Not Found${ColorReset}")
endif()
if(JACK_FOUND)
    message(STATUS "  JACK:              ${Green}Found${ColorReset}")
endif()
message(STATUS "")
message(STATUS "Build Commands:")
message(STATUS "  Configure Debug:     cmake -B build -DCMAKE_BUILD_TYPE=Debug     # With sanitizers (default)")
message(STATUS "  Configure Dev:       cmake -B build -DCMAKE_BUILD_TYPE=Dev       # Debug without sanitizers (faster)")
message(STATUS "  Configure Release:   cmake -B build -DCMAKE_BUILD_TYPE=Release")
message(STATUS "  Configure Coverage:  cmake -B build -DCMAKE_BUILD_TYPE=Coverage")
message(STATUS "  Build:               cmake --build build")
message(STATUS "  Run:                 ./build/bin/ascii-chat [--help|--version] [client|server] [options...]")
if(CRITERION_FOUND)
    message(STATUS "  Run tests:           ./tests/scripts/run_tests.sh")
    message(STATUS "  Run tests (CMake):   cmake --build build --target test_all")
endif()
message(STATUS "  Format code:         cmake --build build --target format")
message(STATUS "  Check formatting:    cmake --build build --target format-check")
message(STATUS "  Install deps:        cmake --build build --target deps")
message(STATUS "========================================")
message(STATUS "")
