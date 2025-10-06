cmake_minimum_required(VERSION 3.16)

# Set policy for ExternalProject timestamp handling (CMP0135)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Set up vcpkg toolchain if available (must be before project())
# Skip vcpkg for MinGW builds as we use system package manager for MinGW
if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT USE_MINGW)
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

    # Find musl-gcc compiler
    find_program(MUSL_GCC musl-gcc)
    if(NOT MUSL_GCC)
        message(FATAL_ERROR "musl-gcc not found. Install musl-tools:\n"
                            "  Arch Linux: sudo pacman -S musl\n"
                            "  Ubuntu/Debian: sudo apt install musl-tools musl-dev")
    endif()

    # Find the real GCC (not ccache wrapper) for musl-gcc to use
    find_program(REAL_GCC NAMES gcc PATHS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
    if(REAL_GCC)
        # Set environment variable for musl-gcc wrapper script
        # This must be done before project() so CMake's compiler tests work
        set(ENV{REALGCC} "${REAL_GCC}")
        message(STATUS "Set REALGCC environment variable: ${REAL_GCC}")

        # Also set compiler launcher for build phase (after project() succeeds)
        set(CMAKE_C_COMPILER_LAUNCHER "env" "REALGCC=${REAL_GCC}" CACHE STRING "Compiler launcher" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "env" "REALGCC=${REAL_GCC}" CACHE STRING "Compiler launcher" FORCE)
    endif()

    # Set compiler to musl-gcc BEFORE project()
    set(CMAKE_C_COMPILER "${MUSL_GCC}" CACHE FILEPATH "C compiler" FORCE)
endif()

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
# Generate version information with git commit hash (regenerated on every build)
# =============================================================================

# Create a script that generates version.h on every build
set(VERSION_SCRIPT "${CMAKE_BINARY_DIR}/generate_version.cmake")
file(WRITE "${VERSION_SCRIPT}" "
# Get git describe output (includes commits since last tag)
execute_process(
    COMMAND git describe --tags --long --dirty --always
    WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\"
    OUTPUT_VARIABLE GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Parse git describe output
# Format: v0.1.0-42-gf811525-dirty
# where 42 is commits since tag, g prefix indicates git hash
if(GIT_DESCRIBE MATCHES \"^v?[0-9]+\\\\.[0-9]+\\\\.[0-9]+-([0-9]+)-g([0-9a-f]+)(-dirty)?\\\$\")
    # Has a tag with commits since
    set(GIT_COMMITS_SINCE_TAG \"\${CMAKE_MATCH_1}\")
    set(GIT_COMMIT_HASH \"\${CMAKE_MATCH_2}\")
    set(GIT_DIRTY_SUFFIX \"\${CMAKE_MATCH_3}\")
    set(GIT_VERSION_STRING \"\${GIT_COMMITS_SINCE_TAG}+g\${GIT_COMMIT_HASH}\${GIT_DIRTY_SUFFIX}\")
elseif(GIT_DESCRIBE MATCHES \"^([0-9a-f]+)(-dirty)?\\\$\")
    # No tags, just commit hash
    set(GIT_COMMIT_HASH \"\${CMAKE_MATCH_1}\")
    set(GIT_DIRTY_SUFFIX \"\${CMAKE_MATCH_2}\")
    set(GIT_VERSION_STRING \"g\${GIT_COMMIT_HASH}\${GIT_DIRTY_SUFFIX}\")
else()
    # Fallback
    set(GIT_VERSION_STRING \"unknown\")
endif()

# Set build type
set(VERSION_BUILD_TYPE \"${CMAKE_BUILD_TYPE}\")

# Set version components
set(PROJECT_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(PROJECT_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(PROJECT_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(PROJECT_VERSION \"${PROJECT_VERSION}\")

# Generate version header
configure_file(
    \"${CMAKE_SOURCE_DIR}/lib/version.h.in\"
    \"${CMAKE_BINARY_DIR}/generated/version.h\"
    @ONLY
)
")

# Add custom target that runs the version script on every build
add_custom_target(generate_version
    COMMAND ${CMAKE_COMMAND} -P "${VERSION_SCRIPT}"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/version.h"
    COMMENT "Generating version header with current git state..."
    VERBATIM
)

# =============================================================================
# Windows-Specific Linking Fixes (must come after project())
# =============================================================================

# Fix Windows-Clang platform issues with -nostartfiles and -nostdlib
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # CMake's Windows-Clang platform files incorrectly add -nostartfiles -nostdlib
    # These flags are inappropriate for normal userspace applications like ASCII-Chat
    # and prevent AddressSanitizer from finding required runtime libraries

    # Remove the problematic flags if they exist in CMAKE_C_STANDARD_LIBRARIES
    string(REPLACE "-nostdlib" "" CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES}")
    string(REPLACE "-nostartfiles" "" CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES}")

    # Remove from linker flags too
    string(REPLACE "-nostdlib" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-nostartfiles" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-nostdlib" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    string(REPLACE "-nostartfiles" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")

    # For Debug and Dev builds, ensure standard runtime libraries are available
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
        # Standard runtime libraries are required for Windows builds
        # Try to find MSVC installation in common locations and editions
        set(MSVC_LIB_PATH "")

        # Try different Visual Studio editions and years
        set(VS_EDITIONS "Community" "Professional" "Enterprise" "BuildTools" "Insiders")
        set(VS_YEARS "18" "2026" "2022" "2019" "2017")

        foreach(VS_YEAR ${VS_YEARS})
            foreach(VS_EDITION ${VS_EDITIONS})
                set(VS_PATH "C:/Program Files/Microsoft Visual Studio/${VS_YEAR}/${VS_EDITION}/VC/Tools/MSVC")
                if(EXISTS "${VS_PATH}")
                    file(GLOB MSVC_VERSIONS "${VS_PATH}/*")
                    if(MSVC_VERSIONS)
                        list(GET MSVC_VERSIONS -1 MSVC_VERSION_DIR)  # Get the latest version
                        set(MSVC_LIB_CANDIDATE "${MSVC_VERSION_DIR}/lib/x64")
                        if(EXISTS "${MSVC_LIB_CANDIDATE}")
                            set(MSVC_LIB_PATH "${MSVC_LIB_CANDIDATE}")
                            message(STATUS "Found MSVC libraries at: ${MSVC_LIB_PATH}")
                            break()
                        endif()
                    endif()
                endif()
            endforeach()
            if(MSVC_LIB_PATH)
                break()
            endif()
        endforeach()

        if(MSVC_LIB_PATH)
            link_directories("${MSVC_LIB_PATH}")
            message(STATUS "Added MSVC library path: ${MSVC_LIB_PATH}")
        else()
            message(FATAL_ERROR "Visual Studio MSVC libraries not found! Windows builds require Visual Studio runtime libraries.\n"
                              "Please install Visual Studio 2017, 2019, 2022, or 2026 Insiders (Community, Professional, Enterprise, Build Tools, or Insiders).\n"
                              "Alternatively, try building with MinGW: ./build.ps1 -MinGW")
        endif()

        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            message(STATUS "Debug build: Preserving standard libraries for AddressSanitizer")
        else()
            message(STATUS "Dev build: Using standard libraries without sanitizers")
        endif()
    endif()

    message(STATUS "Applied Windows-Clang linking fixes for normal userspace application")
endif()

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

# 5. macOS: Auto-detect and prefer Homebrew LLVM over system compiler
if(APPLE)
    # Check if Homebrew LLVM is installed
    set(HOMEBREW_LLVM_PREFIX "")

    # Check common Homebrew installation paths
    if(EXISTS "/usr/local/opt/llvm/bin/clang")
        set(HOMEBREW_LLVM_PREFIX "/usr/local/opt/llvm")
    elseif(EXISTS "/opt/homebrew/opt/llvm/bin/clang")
        set(HOMEBREW_LLVM_PREFIX "/opt/homebrew/opt/llvm")
    endif()

    if(HOMEBREW_LLVM_PREFIX)
        message(STATUS "Found Homebrew LLVM at: ${HOMEBREW_LLVM_PREFIX}")

        # Check if we should use Homebrew LLVM
        set(USE_HOMEBREW_LLVM TRUE)

        # Only skip if user explicitly set CMAKE_C_COMPILER to a non-system compiler
        if(DEFINED CMAKE_C_COMPILER)
            # Check if it's a ccache wrapper or system compiler
            if(NOT CMAKE_C_COMPILER MATCHES "ccache" AND
               NOT CMAKE_C_COMPILER MATCHES "^/usr/bin" AND
               NOT CMAKE_C_COMPILER MATCHES "Xcode" AND
               NOT CMAKE_C_COMPILER STREQUAL "CMAKE_C_COMPILER-NOTFOUND" AND
               CMAKE_C_COMPILER MATCHES "llvm")
                # User explicitly set LLVM compiler, respect it
                set(USE_HOMEBREW_LLVM FALSE)
                message(STATUS "Using user-specified LLVM compiler: ${CMAKE_C_COMPILER}")
            endif()
        endif()

        if(USE_HOMEBREW_LLVM)
            message(STATUS "Configuring to use Homebrew LLVM")

            # Check for ccache and use it if available
            find_program(CCACHE_PROGRAM ccache)
            if(CCACHE_PROGRAM)
                message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
                set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
                # Set the actual compilers (ccache will wrap them via the launcher)
                set(CMAKE_C_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang" CACHE FILEPATH "C compiler" FORCE)
                message(STATUS "Using ccache with Homebrew LLVM")
            else()
                # No ccache, use LLVM directly
                set(CMAKE_C_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang" CACHE FILEPATH "C compiler" FORCE)
            endif()

            # Add LLVM bin directory to PATH for tools like llvm-ar, llvm-ranlib, etc.
            set(CMAKE_PREFIX_PATH "${HOMEBREW_LLVM_PREFIX}" ${CMAKE_PREFIX_PATH})

            # Add LLVM CMake modules to module path for advanced features
            if(EXISTS "${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm")
                list(APPEND CMAKE_MODULE_PATH "${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm")
                message(STATUS "Added LLVM CMake modules: ${HOMEBREW_LLVM_PREFIX}/lib/cmake/llvm")
            endif()

            # Set LLVM tool paths
            set(CMAKE_AR "${HOMEBREW_LLVM_PREFIX}/bin/llvm-ar" CACHE FILEPATH "Archiver" FORCE)
            set(CMAKE_RANLIB "${HOMEBREW_LLVM_PREFIX}/bin/llvm-ranlib" CACHE FILEPATH "Ranlib" FORCE)

            message(STATUS "Configured Homebrew LLVM: ${CMAKE_C_COMPILER}")
        endif()
    else()
        message(STATUS "Homebrew LLVM not found, using system compiler")
    endif()
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

# Configure Homebrew LLVM paths after project() if compiler was set
if(APPLE)
    # Check if we're using Homebrew LLVM (either auto-detected or user-specified)
    if(CMAKE_C_COMPILER)
        get_filename_component(COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(COMPILER_PREFIX "${COMPILER_DIR}" DIRECTORY)

        # Also check for ccache wrapper pointing to Homebrew LLVM
        set(IS_HOMEBREW_LLVM FALSE)
        if(COMPILER_PREFIX MATCHES "/(usr/local|opt/homebrew)/opt/llvm")
            set(IS_HOMEBREW_LLVM TRUE)
        elseif(COMPILER_PREFIX MATCHES "ccache" AND EXISTS "/usr/local/opt/llvm/bin/clang")
            # ccache wrapper detected, check if underlying compiler is Homebrew LLVM
            set(COMPILER_PREFIX "/usr/local/opt/llvm")
            set(IS_HOMEBREW_LLVM TRUE)
        elseif(COMPILER_PREFIX MATCHES "ccache" AND EXISTS "/opt/homebrew/opt/llvm/bin/clang")
            set(COMPILER_PREFIX "/opt/homebrew/opt/llvm")
            set(IS_HOMEBREW_LLVM TRUE)
        endif()

        if(IS_HOMEBREW_LLVM)
            message(STATUS "Configuring Homebrew LLVM paths at: ${COMPILER_PREFIX}")

            # Homebrew LLVM's Clang needs to know where the macOS SDK is for standard headers
            # Get the SDK path using xcrun (this is critical for finding stdbool.h, etc.)
            execute_process(
                COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE MACOS_SDK_PATH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )

            if(MACOS_SDK_PATH)
                message(STATUS "Found macOS SDK at: ${MACOS_SDK_PATH}")
                # Add SDK flags to compiler and linker
                set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -isysroot ${MACOS_SDK_PATH}" CACHE STRING "C flags" FORCE)
                set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isysroot ${MACOS_SDK_PATH}" CACHE STRING "C++ flags" FORCE)
                set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -isysroot ${MACOS_SDK_PATH}" CACHE STRING "Linker flags" FORCE)
                set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -isysroot ${MACOS_SDK_PATH}" CACHE STRING "Shared linker flags" FORCE)
            else()
                message(WARNING "Could not find macOS SDK path - standard headers may not be found")
            endif()

            # Note: Do NOT add LLVM include paths globally - this breaks mimalloc and other
            # dependencies by overriding standard system headers. The LLVM Clang compiler
            # automatically knows where to find its own headers (intrinsics, etc.) through
            # its resource directory. Only add LLVM includes to specific targets if needed.
            # include_directories(SYSTEM "${COMPILER_PREFIX}/include")  # REMOVED - breaks builds

            # Add LLVM library paths and libraries (equivalent to LDFLAGS from brew info llvm)
            link_directories("${COMPILER_PREFIX}/lib")

            # Add the full LDFLAGS as recommended by brew info llvm
            # This includes libc++, libunwind which are needed for full LLVM toolchain
            # Store the flags but don't add -lunwind globally to avoid duplication
            set(HOMEBREW_LLVM_LINK_DIRS "-L${COMPILER_PREFIX}/lib -L${COMPILER_PREFIX}/lib/c++ -L${COMPILER_PREFIX}/lib/unwind")
            set(HOMEBREW_LLVM_LIBS "unwind")

            if(NOT DEFINED ENV{LDFLAGS} OR NOT "$ENV{LDFLAGS}" MATCHES "-L.*llvm")
                # Add only the library search paths globally (not the actual libraries)
                # Check if paths are already present to avoid duplication
                if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "-L.*llvm/lib/unwind")
                    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${HOMEBREW_LLVM_LINK_DIRS}" CACHE STRING "Linker flags" FORCE)
                    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${HOMEBREW_LLVM_LINK_DIRS}" CACHE STRING "Shared linker flags" FORCE)
                    message(STATUS "Added Homebrew LLVM library paths (not in environment)")
                else()
                    message(STATUS "Homebrew LLVM library paths already present in linker flags")
                endif()
            else()
                message(STATUS "Skipping Homebrew LLVM library paths (already in LDFLAGS environment variable)")
            endif()

            # Store for later sanitizer configuration
            set(HOMEBREW_LLVM_LIB_DIR "${COMPILER_PREFIX}/lib")

            message(STATUS "Applied Homebrew LLVM toolchain flags:")
            message(STATUS "  Include: (using compiler's resource directory - NOT added globally)")
            if(NOT DEFINED ENV{LDFLAGS} OR NOT "$ENV{LDFLAGS}" MATCHES "-L.*llvm")
                message(STATUS "  Link: ${HOMEBREW_LLVM_LINK_DIRS}")
            else()
                message(STATUS "  Link: (using LDFLAGS environment variable)")
            endif()
        endif()
    endif()
endif()

# Configure MinGW mode for any compiler
if(WIN32 AND USE_MINGW)
    message(STATUS "Configuring ${CMAKE_C_COMPILER_ID} for MinGW mode")

    # Set MinGW target and system paths for Clang (GCC doesn't need this)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_C_COMPILER_TARGET x86_64-w64-mingw32)

        # Find MinGW system headers - check common installation paths
        set(MINGW_INCLUDE_PATHS
            "$ENV{USERPROFILE}/scoop/apps/gcc/current/include"  # Scoop GCC (current user)
            "$ENV{USERPROFILE}/scoop/apps/gcc/current/x86_64-w64-mingw32/include"  # Scoop GCC target-specific
            "$ENV{SCOOP}/apps/gcc/current/include"  # Scoop global install
            "$ENV{SCOOP}/apps/gcc/current/x86_64-w64-mingw32/include"  # Scoop global target-specific
            "C:/mingw64/include"  # MSYS2 MinGW64
            "C:/msys64/mingw64/include"  # MSYS2 full install
            "/mingw64/include"  # Unix-style path
            "${CMAKE_C_COMPILER}/../include"  # Relative to compiler
            "${CMAKE_C_COMPILER}/../../x86_64-w64-mingw32/include"  # GCC target include
        )

        foreach(INCLUDE_PATH IN LISTS MINGW_INCLUDE_PATHS)
            if(EXISTS "${INCLUDE_PATH}/stdio.h")
                include_directories(SYSTEM "${INCLUDE_PATH}")
                message(STATUS "Found MinGW system headers at: ${INCLUDE_PATH}")
                break()
            endif()
        endforeach()
    endif()

    # Use MinGW linker for all compilers
    set(CMAKE_C_LINK_EXECUTABLE "${CMAKE_C_COMPILER} <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

    # Override CMake's Windows-specific compile flags that are set by the platform module
    # These get added by CMake's Windows-Clang.cmake platform file

    # Override all the flag variables that CMake sets for Windows
    set(CMAKE_C_FLAGS "" CACHE STRING "C flags" FORCE)
    set(CMAKE_C_FLAGS_DEBUG "-O0 -g -Wall -Wextra -DDEBUG" CACHE STRING "C Debug flags" FORCE)
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C Release flags" FORCE)

    message(STATUS "MinGW build using clean compiler flags")
endif()

# Clear Windows-specific CMake flags for native Clang builds
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT USE_MINGW)
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

# If using musl, disable ccache and sanitizers since musl-gcc wraps GCC
# GCC doesn't support the same sanitizer flags as Clang
if(USE_MUSL)
    set(USE_CCACHE OFF CACHE BOOL "Disable ccache when using musl" FORCE)

    # Force Dev build type (debug symbols without sanitizers) if Debug was requested
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Changing build type from Debug to Dev - GCC (via musl-gcc) doesn't support all Clang sanitizers")
        set(CMAKE_BUILD_TYPE "Dev" CACHE STRING "Build type" FORCE)
    endif()
endif()

# =============================================================================
# ccache Support (Compiler Cache for Faster Rebuilds)
# =============================================================================

# Find and enable ccache if available (unless explicitly disabled)
option(USE_CCACHE "Use ccache for compilation if available" ON)

if(USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        # Only set ccache if not already configured (e.g., by Homebrew LLVM setup)
        if(NOT CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
            message(STATUS "${BoldGreen}Using ccache: ${CCACHE_PROGRAM}${ColorReset}")

            # Configure ccache for optimal performance
            execute_process(COMMAND ${CCACHE_PROGRAM} --set-config max_size=2G)
            execute_process(COMMAND ${CCACHE_PROGRAM} --set-config compression=true)
            execute_process(COMMAND ${CCACHE_PROGRAM} --set-config compression_level=6)
        else()
            message(STATUS "Compiler launcher already set: ${CMAKE_C_COMPILER_LAUNCHER}")
        endif()
    else()
        message(STATUS "ccache not found - install with: sudo pacman -S ccache")
    endif()
endif()

# =============================================================================
# Platform Detection
# =============================================================================

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

elseif(WIN32)
    add_definitions(-D_WIN32 -DWIN32_LEAN_AND_MEAN)
    set(PLATFORM_WINDOWS TRUE)

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


# CRITICAL: mimalloc's MI_MALLOC_OVERRIDE is incompatible with AddressSanitizer
# Both try to intercept malloc/free, causing initialization order crashes during __asan_init
# Force Dev build type (debug without sanitizers) when USE_MIMALLOC is ON
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND USE_MIMALLOC)
    message(WARNING "Switching from Debug to Dev build - AddressSanitizer conflicts with mimalloc malloc override")
    set(CMAKE_BUILD_TYPE "Dev" CACHE STRING "Build type" FORCE)
endif()
# Build type specific flags (matching Makefile logic)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug mode WITH sanitizers (default, safe)
    # Don't add DEBUG_MEMORY if mimalloc is enabled - mimalloc provides its own memory tracking
    # Don't add DEBUG_MEMORY if musl is enabled - musl's strict aliasing breaks the macros
    if(NOT USE_MIMALLOC AND NOT USE_MUSL)
        add_definitions(-DDEBUG_MEMORY)
    elseif(USE_MIMALLOC)
        message(STATUS "DEBUG_MEMORY disabled - using mimalloc's memory tracking instead")
        # Enable mimalloc debugging features (MI_DEBUG will be set per-target to avoid conflicts)
        add_definitions(-DUSE_MIMALLOC_DEBUG)
        set(MIMALLOC_DEBUG_LEVEL 2)  # MI_DEBUG level 2: basic checks + internal assertions
    elseif(USE_MUSL)
        message(STATUS "DEBUG_MEMORY disabled - musl's strict aliasing is incompatible with DEBUG_MEMORY macros")
    endif()
    if(WIN32 AND USE_MINGW)
        # MinGW mode already has flags set - skip to avoid conflicts
        message(STATUS "Skipping debug flag setup for MinGW (already configured)")
    else()
        add_compile_options(-g -O0 -DDEBUG)

        # Always use sanitizers in Debug mode
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            if(WIN32)
                # Windows with Clang - use all sanitizers that work on Windows
                # AddressSanitizer + comprehensive UndefinedBehaviorSanitizer checks
                add_compile_options(
                    -fsanitize=address
                    -fsanitize=undefined
                    -fsanitize=integer
                    -fsanitize=nullability
                    -fsanitize=implicit-conversion
                    -fsanitize=float-divide-by-zero
                    -fsanitize-address-use-after-scope
                    -fno-omit-frame-pointer
                    -fno-optimize-sibling-calls
                )
                add_link_options(
                    -fsanitize=address
                    -fsanitize=undefined
                    -fsanitize=integer
                    -fsanitize=nullability
                    -fsanitize=implicit-conversion
                    -fsanitize=float-divide-by-zero
                )
                message(STATUS "Debug build: Enabled ASan + UBSan + Integer + Nullability + ImplicitConversion sanitizers")
            elseif(APPLE)
                # macOS - all sanitizers except thread and leak (not supported on ARM64)
                add_compile_options(
                    -fsanitize=address
                    -fsanitize=undefined
                    -fsanitize=integer
                    -fsanitize=nullability
                    -fsanitize=implicit-conversion
                    -fsanitize=float-divide-by-zero
                    -fsanitize-address-use-after-scope
                    -fno-omit-frame-pointer
                    -fno-optimize-sibling-calls
                )
                add_link_options(
                    -fsanitize=address
                    -fsanitize=undefined
                    -fsanitize=integer
                    -fsanitize=nullability
                    -fsanitize=implicit-conversion
                    -fsanitize=float-divide-by-zero
                )
                message(STATUS "Debug build: Enabled ASan + UBSan + Integer + Nullability + ImplicitConversion sanitizers")
            else()
                # Linux - all sanitizers except thread (conflicts with address)
                add_compile_options(
                    -fsanitize=address
                    -fsanitize=undefined
                    -fsanitize=leak
                    -fsanitize=integer
                    -fsanitize=nullability
                    -fsanitize=implicit-conversion
                    -fsanitize=float-divide-by-zero
                    -fsanitize-address-use-after-scope
                    -fno-omit-frame-pointer
                    -fno-optimize-sibling-calls
                )
                add_link_options(
                    -fsanitize=address
                    -fsanitize=undefined
                    -fsanitize=leak
                    -fsanitize=integer
                    -fsanitize=nullability
                    -fsanitize=implicit-conversion
                    -fsanitize=float-divide-by-zero
                )
                message(STATUS "Debug build: Enabled ASan + LeakSan + UBSan + Integer + Nullability + ImplicitConversion sanitizers")
            endif()
        elseif(CMAKE_C_COMPILER_ID MATCHES "GNU" AND NOT WIN32)
            # GCC on Unix - use available sanitizers
            add_compile_options(
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=leak
                -fsanitize-address-use-after-scope
                -fno-omit-frame-pointer
                -fno-optimize-sibling-calls
            )
            add_link_options(
                -fsanitize=address
                -fsanitize=undefined
                -fsanitize=leak
            )
            message(STATUS "Debug build: Enabled ASan + LeakSan + UBSan (GCC)")
        endif()

        # macOS: Fix ASan runtime linking for Homebrew LLVM
        if(APPLE AND DEFINED HOMEBREW_LLVM_LIB_DIR)
            # Find the clang version directory dynamically
            file(GLOB CLANG_VERSION_DIRS "${HOMEBREW_LLVM_LIB_DIR}/clang/*")
            if(CLANG_VERSION_DIRS)
                list(GET CLANG_VERSION_DIRS 0 CLANG_VERSION_DIR)
                get_filename_component(CLANG_VERSION_NAME "${CLANG_VERSION_DIR}" NAME)
                # Explicitly link against the correct ASan runtime from Homebrew LLVM
                add_link_options(-L${HOMEBREW_LLVM_LIB_DIR}/clang/${CLANG_VERSION_NAME}/lib/darwin)
                # Force rpath for ASan runtime
                add_link_options(-rpath ${HOMEBREW_LLVM_LIB_DIR}/clang/${CLANG_VERSION_NAME}/lib/darwin)
                message(STATUS "Using ASan runtime from: ${HOMEBREW_LLVM_LIB_DIR}/clang/${CLANG_VERSION_NAME}/lib/darwin")
            endif()
        endif()

        # On Windows with Clang, copy ASAN runtime DLL to output directory
        if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
            # Find the ASAN DLL in the Clang installation
            get_filename_component(CLANG_DIR ${CMAKE_C_COMPILER} DIRECTORY)
            get_filename_component(CLANG_ROOT ${CLANG_DIR} DIRECTORY)
            file(GLOB_RECURSE ASAN_DLL "${CLANG_ROOT}/lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
            if(ASAN_DLL)
                # Copy ASAN DLL to build output directory
                list(GET ASAN_DLL 0 ASAN_DLL_PATH)
                file(COPY ${ASAN_DLL_PATH} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
                message(STATUS "Copied ASAN runtime for Debug build: ${ASAN_DLL_PATH}")
            endif()
        endif()

        if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT USE_MINGW)
            # CodeView debug format is only supported by Clang on Windows (native mode)
            add_compile_options(-gcodeview)
        endif()
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Dev")
    # Dev mode - debug WITHOUT sanitizers (faster iteration)
    # Don't add DEBUG_MEMORY if mimalloc is enabled - mimalloc provides its own memory tracking
    # Don't add DEBUG_MEMORY if musl is enabled - musl's strict aliasing breaks the macros
    if(NOT USE_MIMALLOC AND NOT USE_MUSL)
        add_definitions(-DDEBUG_MEMORY)
    elseif(USE_MIMALLOC)
        message(STATUS "DEBUG_MEMORY disabled - using mimalloc's memory tracking instead")
        # Enable mimalloc debugging features (MI_DEBUG will be set per-target to avoid conflicts)
        add_definitions(-DUSE_MIMALLOC_DEBUG)
        set(MIMALLOC_DEBUG_LEVEL 2)  # MI_DEBUG level 2: basic checks + internal assertions
    elseif(USE_MUSL)
        message(STATUS "DEBUG_MEMORY disabled - musl's strict aliasing is incompatible with DEBUG_MEMORY macros")
    endif()
    if(WIN32 AND USE_MINGW)
        # MinGW mode already has flags set - skip to avoid conflicts
        message(STATUS "Skipping dev flag setup for MinGW (already configured)")
    else()
        add_compile_options(-g -O0 -DDEBUG)
        # No sanitizers in Dev mode for faster compilation/runtime

        if(WIN32)
            if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT USE_MINGW)
                # CodeView debug format is only supported by Clang on Windows (native mode)
                # Use -g2 for full debug info and ensure PDB generation
                add_compile_options(-g2 -gcodeview)
                # Ensure PDB files are generated - linker will create them by default with /DEBUG
                add_link_options(-Wl,/DEBUG:FULL)
                message(STATUS "Dev build: Enabled PDB generation with full debug info")
            elseif(MSVC)
                # For MSVC, explicitly enable PDB generation
                add_compile_options(/Zi)
                add_link_options(/DEBUG:FULL)
                message(STATUS "Dev build: Enabled MSVC PDB generation")
            endif()
        endif()
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    add_definitions(-DDEBUG_MEMORY -DCOVERAGE_BUILD)
    add_compile_options(-g -O0 --coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-DNDEBUG)
    # CPU-aware optimization flags (matches Makefile)
    add_compile_options(-O3 -funroll-loops -fstrict-aliasing -ftree-vectorize -fomit-frame-pointer -pipe)
    if(PLATFORM_DARWIN)
        if(IS_ROSETTA EQUAL 1)
            add_compile_options(-march=native -ffp-contract=fast -ffinite-math-only)
        elseif(IS_APPLE_SILICON EQUAL 1)
            add_compile_options(-march=native -mcpu=native -ffast-math -ffp-contract=fast)
        else()
            add_compile_options(-march=native -ffp-contract=fast -ffinite-math-only)
        endif()
    elseif(PLATFORM_LINUX)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
            add_compile_options(-mcpu=native -ffp-contract=fast -ffinite-math-only)
        else()
            add_compile_options(-march=native -ffp-contract=fast -ffinite-math-only)
        endif()
    else()
        add_compile_options(-ffp-contract=fast)
    endif()

    if(NOT WIN32)
        add_compile_options(-flto -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-trapping-math -falign-loops=32 -falign-functions=32 -fno-plt -fno-semantic-interposition -fmerge-all-constants)
        add_link_options(-flto -fno-plt)
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Sanitize")
    add_definitions(-DDEBUG_MEMORY)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR (CMAKE_C_COMPILER_ID MATCHES "GNU" AND NOT WIN32))
        add_compile_options(-g -O0 -fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)

        # On Windows with Clang, copy ASAN runtime DLL to output directory
        if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
            # Find the ASAN DLL in the Clang installation
            get_filename_component(CLANG_DIR ${CMAKE_C_COMPILER} DIRECTORY)
            get_filename_component(CLANG_ROOT ${CLANG_DIR} DIRECTORY)
            file(GLOB_RECURSE ASAN_DLL "${CLANG_ROOT}/lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
            if(ASAN_DLL)
                # Copy ASAN DLL to build output directory
                list(GET ASAN_DLL 0 ASAN_DLL_PATH)
                file(COPY ${ASAN_DLL_PATH} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
                message(STATUS "Copied ASAN runtime: ${ASAN_DLL_PATH}")
            else()
                message(WARNING "Could not find ASAN runtime DLL in Clang installation")
            endif()
        endif()
    else()
        # Fallback for compilers without sanitizer support
        add_compile_options(-g -O0)
        message(WARNING "AddressSanitizer not available for this compiler/platform combination")
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "TSan")
    # Thread Sanitizer for race condition detection
    add_definitions(-DDEBUG_MEMORY -DDEBUG_THREADS)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        if(WIN32)
            # ThreadSanitizer has limited support on Windows with Clang
            # It may not work fully, but we can try
            add_compile_options(-g -O1 -fsanitize=thread -fno-omit-frame-pointer)
            add_link_options(-fsanitize=thread)
            message(WARNING "ThreadSanitizer has limited support on Windows. Consider using Linux/macOS for full TSan support.")
        else()
            # Full TSan support on Unix-like systems
            add_compile_options(-g -O1 -fsanitize=thread -fno-omit-frame-pointer -fPIE)
            add_link_options(-fsanitize=thread -pie)
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU" AND NOT WIN32)
        # GCC TSan support (Unix only)
        add_compile_options(-g -O1 -fsanitize=thread -fno-omit-frame-pointer -fPIE)
        add_link_options(-fsanitize=thread -pie)
    else()
        # Fallback for compilers without TSan support
        add_compile_options(-g -O1)
        message(WARNING "ThreadSanitizer not available for this compiler/platform combination")
        message(STATUS "Consider using Clang or GCC on Linux/macOS for ThreadSanitizer support")
    endif()
endif()

# Hardware Acceleration Detection (cross-platform including Windows ARM)
# =============================================================================

include(CheckCSourceCompiles)
include(CheckCSourceRuns)

# User override controls
set(SIMD_MODE "auto" CACHE STRING "SIMD mode: auto, sse2, ssse3, avx2, neon, sve")
set_property(CACHE SIMD_MODE PROPERTY STRINGS "auto" "sse2" "ssse3" "avx2" "neon" "sve")

# Initialize SIMD flags
set(ENABLE_SIMD_SSE2 FALSE)
set(ENABLE_SIMD_SSSE3 FALSE)
set(ENABLE_SIMD_AVX2 FALSE)
set(ENABLE_SIMD_NEON FALSE)
set(ENABLE_SIMD_SVE FALSE)

# Check for user-specified SIMD mode
if(NOT SIMD_MODE STREQUAL "auto")
    # Manual mode - only enable the specific architecture requested
    if(SIMD_MODE STREQUAL "sse2")
        set(ENABLE_SIMD_SSE2 TRUE)
    elseif(SIMD_MODE STREQUAL "ssse3")
        set(ENABLE_SIMD_SSSE3 TRUE)
    elseif(SIMD_MODE STREQUAL "avx2")
        set(ENABLE_SIMD_AVX2 TRUE)
    elseif(SIMD_MODE STREQUAL "neon")
        set(ENABLE_SIMD_NEON TRUE)
    elseif(SIMD_MODE STREQUAL "sve")
        set(ENABLE_SIMD_SVE TRUE)
    endif()
else()
    # Auto-detect SIMD capabilities (cross-platform)

    # Windows-specific detection
    if(WIN32)
        # Check processor architecture on Windows
        # CMAKE_SYSTEM_PROCESSOR on Windows: AMD64, x86, ARM64, ARM
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
            # Windows on ARM64 - ARMv8 always has NEON
            set(ENABLE_SIMD_NEON TRUE)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM")
            # Windows on ARM32 - NEON is optional in ARMv7
            # Try compile test to check for NEON
            check_c_source_compiles("
                #include <arm_neon.h>
                int main() {
                    uint8x16_t a = vdupq_n_u8(0);
                    return 0;
                }
            " CAN_COMPILE_NEON)
            if(CAN_COMPILE_NEON)
                set(ENABLE_SIMD_NEON TRUE)
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
            # Windows x86_64 - try compile tests for SIMD features
            # Try to compile and run CPUID check for x86
            # Note: Clang on Windows may have issues with check_c_source_runs, so we use compile checks instead
            if(NOT CMAKE_CROSSCOMPILING AND NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))
                # Check from highest to lowest, stop when we find support
                # Higher instruction sets imply lower ones

                # Check for AVX2
                check_c_source_runs("
                    #include <intrin.h>
                    int main() {
                        int info[4];
                        __cpuidex(info, 7, 0);
                        if (info[1] & (1 << 5)) // AVX2
                            return 0;
                        return 1;
                    }
                " HAS_AVX2_RUNTIME)

                if(HAS_AVX2_RUNTIME)
                    set(ENABLE_SIMD_AVX2 TRUE)
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)
                else()
                    # Check for SSSE3
                    check_c_source_runs("
                        #include <intrin.h>
                        int main() {
                            int info[4];
                            __cpuid(info, 1);
                            if (info[2] & (1 << 9)) // SSSE3
                                return 0;
                            return 1;
                        }
                    " HAS_SSSE3_RUNTIME)

                    if(HAS_SSSE3_RUNTIME)
                        set(ENABLE_SIMD_SSSE3 TRUE)
                        set(ENABLE_SIMD_SSE2 TRUE)
                    else()
                        # Check for SSE2 (baseline for x86_64)
                        check_c_source_runs("
                            #include <intrin.h>
                            int main() {
                                int info[4];
                                __cpuid(info, 1);
                                if (info[3] & (1 << 26)) // SSE2
                                    return 0;
                                return 1;
                            }
                        " HAS_SSE2_RUNTIME)

                        if(HAS_SSE2_RUNTIME)
                            set(ENABLE_SIMD_SSE2 TRUE)
                        endif()
                    endif()
                endif()
            else()
                # Cross-compiling or Clang on Windows, use compile-time checks with appropriate flags
                # Save current flags
                set(CMAKE_REQUIRED_FLAGS_SAVE ${CMAKE_REQUIRED_FLAGS})

                # Test AVX2 with required flags
                set(CMAKE_REQUIRED_FLAGS "-mavx2")
                check_c_source_compiles("
                    #include <immintrin.h>
                    int main() { __m256i a = _mm256_setzero_si256(); return 0; }
                " CAN_COMPILE_AVX2)

                # Test SSSE3 with required flags
                set(CMAKE_REQUIRED_FLAGS "-mssse3")
                check_c_source_compiles("
                    #include <tmmintrin.h>
                    int main() { __m128i a = _mm_setzero_si128(); a = _mm_abs_epi8(a); return 0; }
                " CAN_COMPILE_SSSE3)

                # Test SSE2 with required flags
                set(CMAKE_REQUIRED_FLAGS "-msse2")
                check_c_source_compiles("
                    #include <emmintrin.h>
                    int main() { __m128i a = _mm_setzero_si128(); return 0; }
                " CAN_COMPILE_SSE2)

                # Restore flags
                set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})

                # Enable all instruction sets that can be compiled
                # Note: Higher SIMD levels include lower ones
                if(CAN_COMPILE_AVX2)
                    set(ENABLE_SIMD_AVX2 TRUE)
                    set(ENABLE_SIMD_SSSE3 TRUE)  # AVX2 includes SSSE3
                    set(ENABLE_SIMD_SSE2 TRUE)   # AVX2 includes SSE2
                elseif(CAN_COMPILE_SSSE3)
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)   # SSSE3 includes SSE2
                elseif(CAN_COMPILE_SSE2)
                    set(ENABLE_SIMD_SSE2 TRUE)
                endif()
            endif()
        endif()

    elseif(PLATFORM_DARWIN)
        # macOS detection (native)
        if(IS_APPLE_SILICON EQUAL 1)
            set(ENABLE_SIMD_NEON TRUE)
        elseif(IS_ROSETTA EQUAL 1)
            set(ENABLE_SIMD_SSSE3 TRUE)
            set(ENABLE_SIMD_SSE2 TRUE)  # SSSE3 includes SSE2
        else()
            # Intel Mac - check for AVX2 support
            execute_process(
                COMMAND sysctl -n hw.optional.avx2_0
                OUTPUT_VARIABLE HAS_AVX2_MAC
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(HAS_AVX2_MAC EQUAL 1)
                set(ENABLE_SIMD_AVX2 TRUE)
                set(ENABLE_SIMD_SSSE3 TRUE)  # AVX2 includes SSSE3
                set(ENABLE_SIMD_SSE2 TRUE)   # AVX2 includes SSE2
            else()
                set(ENABLE_SIMD_SSSE3 TRUE)
                set(ENABLE_SIMD_SSE2 TRUE)  # SSSE3 includes SSE2
            endif()
        endif()

    elseif(PLATFORM_LINUX)
        # Linux detection
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            # Linux ARM64 - ARMv8 always has NEON
            set(ENABLE_SIMD_NEON TRUE)
            # Check for SVE (optional extension)
            if(EXISTS "/proc/cpuinfo")
                file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
                if(CPUINFO_CONTENT MATCHES "sve")
                    # SVE is available in addition to NEON
                    set(ENABLE_SIMD_SVE TRUE)
                endif()
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
            # Linux ARM32 - NEON is optional
            if(EXISTS "/proc/cpuinfo")
                file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
                if(CPUINFO_CONTENT MATCHES "neon")
                    set(ENABLE_SIMD_NEON TRUE)
                endif()
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            # Linux x86_64 - use /proc/cpuinfo
            if(EXISTS "/proc/cpuinfo")
                file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
                # Check from highest to lowest, set all implied instruction sets
                if(CPUINFO_CONTENT MATCHES "avx2")
                    set(ENABLE_SIMD_AVX2 TRUE)
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)
                elseif(CPUINFO_CONTENT MATCHES "ssse3")
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)
                elseif(CPUINFO_CONTENT MATCHES "sse2")
                    set(ENABLE_SIMD_SSE2 TRUE)
                endif()
            endif()
        endif()
    endif()
endif()

# Apply SIMD compile definitions and flags
if(ENABLE_SIMD_SSE2 OR ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2 OR ENABLE_SIMD_NEON OR ENABLE_SIMD_SVE)
    add_definitions(-DSIMD_SUPPORT)

    # Prefer wider vector widths for SIMD-heavy workloads
    if(ENABLE_SIMD_AVX2)
        add_compile_options(-mprefer-vector-width=256)
    endif()
endif()

# Apply specific SIMD flags
if(ENABLE_SIMD_SSE2)
    add_definitions(-DSIMD_SUPPORT_SSE2)
    if(WIN32)
        add_compile_options(-msse2 -mno-mmx)  # Disable MMX on Windows
    else()
        add_compile_options(-msse2)
    endif()
endif()

if(ENABLE_SIMD_SSSE3)
    add_definitions(-DSIMD_SUPPORT_SSSE3)
    if(WIN32)
        add_compile_options(-mssse3 -mno-mmx)  # Disable MMX on Windows
    else()
        add_compile_options(-mssse3)
    endif()
endif()

if(ENABLE_SIMD_AVX2)
    add_definitions(-DSIMD_SUPPORT_AVX2)
    if(WIN32)
        add_compile_options(-mavx2 -mno-mmx)  # Disable MMX on Windows
    else()
        add_compile_options(-mavx2)
    endif()
endif()

if(ENABLE_SIMD_NEON)
    add_definitions(-DSIMD_SUPPORT_NEON)
    # Windows ARM64 with Clang needs proper arch flags
    if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|ARM64")
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            # Clang on Windows ARM might need explicit flags
            add_compile_options(-march=armv8-a+simd)
        endif()
    endif()
endif()

if(ENABLE_SIMD_SVE)
    add_definitions(-DSIMD_SUPPORT_SVE)
    add_compile_options(-march=armv8-a+sve)
endif()

# CRC32 Hardware Acceleration
set(CRC32_HW "auto" CACHE STRING "CRC32 hardware acceleration: auto, on, off")
set(ENABLE_CRC32_HW FALSE)

if(CRC32_HW STREQUAL "on")
    set(ENABLE_CRC32_HW TRUE)
elseif(CRC32_HW STREQUAL "auto")
    if(WIN32)
        # Windows CRC32 detection
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|ARM64")
            # Try to detect CRC32 support on ARM
            # ARMv8-A includes optional CRC32, but not all implementations have it
            if(NOT CMAKE_CROSSCOMPILING)
                # Try runtime detection for ARM CRC32
                check_c_source_runs("
                    #include <arm_acle.h>
                    #include <stdint.h>
                    int main() {
                        uint32_t crc = 0;
                        uint8_t data = 0x42;
                        crc = __crc32b(crc, data);
                        return 0;
                    }
                " HAS_ARM_CRC32_RUNTIME)
                if(HAS_ARM_CRC32_RUNTIME)
                    set(ENABLE_CRC32_HW TRUE)
                endif()
            else()
                # Cross-compiling - try compile test
                check_c_source_compiles("
                    #include <arm_acle.h>
                    #include <stdint.h>
                    int main() {
                        uint32_t crc = 0;
                        uint8_t data = 0x42;
                        crc = __crc32b(crc, data);
                        return 0;
                    }
                " CAN_COMPILE_ARM_CRC32)
                if(CAN_COMPILE_ARM_CRC32)
                    set(ENABLE_CRC32_HW TRUE)
                endif()
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
            # Check for SSE4.2 support on x86_64
            # Note: Clang on Windows has issues with check_c_source_runs, so check SIMD flags first
            if(ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2)
                # Assume SSE4.2 if we have SSSE3 or better
                set(ENABLE_CRC32_HW TRUE)
            elseif(NOT CMAKE_CROSSCOMPILING AND NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))
                check_c_source_runs("
                    #include <intrin.h>
                    int main() {
                        int info[4];
                        __cpuid(info, 1);
                        if (info[2] & (1 << 20)) // SSE4.2
                            return 0;
                        return 1;
                    }
                " HAS_SSE42_RUNTIME)
                if(HAS_SSE42_RUNTIME)
                    set(ENABLE_CRC32_HW TRUE)
                endif()
            endif()
        endif()
    elseif(PLATFORM_DARWIN)
        # macOS detection
        if(IS_APPLE_SILICON EQUAL 1)
            # Apple Silicon M1/M2/M3 all have CRC32
            set(ENABLE_CRC32_HW TRUE)
        else()
            # Intel Mac - check for SSE4.2
            execute_process(
                COMMAND sysctl -n hw.optional.sse4_2
                OUTPUT_VARIABLE HAS_SSE42_MAC
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(HAS_SSE42_MAC EQUAL 1)
                set(ENABLE_CRC32_HW TRUE)
            endif()
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM|ARM64")
        # Linux/Generic ARM - try to detect CRC32 support
        if(PLATFORM_LINUX AND EXISTS "/proc/cpuinfo")
            file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
            if(CPUINFO_CONTENT MATCHES "crc32")
                set(ENABLE_CRC32_HW TRUE)
            endif()
        else()
            # Try compile test as fallback
            check_c_source_compiles("
                #include <arm_acle.h>
                #include <stdint.h>
                int main() {
                    uint32_t crc = 0;
                    uint8_t data = 0x42;
                    crc = __crc32b(crc, data);
                    return 0;
                }
            " CAN_COMPILE_ARM_CRC32)
            if(CAN_COMPILE_ARM_CRC32)
                set(ENABLE_CRC32_HW TRUE)
            endif()
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        # Linux x86_64 - assume SSE4.2 if we have SSSE3
        if(ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2)
            set(ENABLE_CRC32_HW TRUE)
        endif()
    endif()
endif()

if(ENABLE_CRC32_HW)
    add_definitions(-DHAVE_CRC32_HW)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        add_compile_options(-msse4.2)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|ARM64|aarch64")
        # ARM CRC32 requires specific compiler flags
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            add_compile_options(-march=armv8-a+crc)
        elseif(CMAKE_C_COMPILER_ID MATCHES "GNU")
            add_compile_options(-march=armv8-a+crc)
        endif()
    endif()
endif()

# =============================================================================
# musl libc Additional Configuration
# =============================================================================
# Note: Compiler setup is done before project() (line ~37)
# This section handles linker flags and other post-project() configuration

if(USE_MUSL)
    # Use -static flag to build fully static binaries with musl
    # This requires static versions of all dependencies
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

    # Disable precompiled headers - musl-gcc uses GCC which has different PCH syntax than Clang
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "Disable PCH for musl-gcc" FORCE)

    message(STATUS "musl C library enabled (static linking mode)")
    message(STATUS "Precompiled headers disabled for musl-gcc compatibility")
    message(STATUS "")
    message(STATUS "NOTE: Static linking enabled - will build static dependencies with musl")

    # Set up paths for musl-built static libraries
    set(MUSL_PREFIX "${CMAKE_BINARY_DIR}/musl-deps")
    set(CMAKE_PREFIX_PATH "${MUSL_PREFIX}" ${CMAKE_PREFIX_PATH})

    # Add musl-deps include directory for libexecinfo headers
    include_directories(${MUSL_PREFIX}/include)

    # Set linker flags for static musl linking
    # -rdynamic exports symbols for backtrace_symbols() to resolve function names
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -rdynamic")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static -rdynamic")

    # Set REALGCC environment variable for musl-gcc wrapper
    # This is needed because musl-gcc is a shell script that wraps gcc
    set(ENV{REALGCC} "/usr/bin/gcc")

    # Wrap musl-gcc with env for linking (CMAKE_C_COMPILER_LAUNCHER only affects compilation)
    # We need to ensure REALGCC is set during linking too
    set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_COMMAND> -E env REALGCC=/usr/bin/gcc <CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
    set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_COMMAND> -E env REALGCC=/usr/bin/gcc <CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

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

    # Build zlib with musl
    ExternalProject_Add(zlib-musl
        URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
        PREFIX ${MUSL_PREFIX}
        CONFIGURE_COMMAND env CC=${MUSL_CC} REALGCC=/usr/bin/gcc CFLAGS=-fPIC <SOURCE_DIR>/configure --prefix=${MUSL_PREFIX} --static
        BUILD_COMMAND env REALGCC=/usr/bin/gcc make
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${MUSL_PREFIX}/lib/libz.a
    )

    # Build libsodium with musl
    ExternalProject_Add(libsodium-musl
        URL https://github.com/jedisct1/libsodium/releases/download/1.0.20-RELEASE/libsodium-1.0.20.tar.gz
        URL_HASH SHA256=ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19
        PREFIX ${MUSL_PREFIX}
        CONFIGURE_COMMAND env CC=${MUSL_CC} REALGCC=/usr/bin/gcc CFLAGS=-fPIC <SOURCE_DIR>/configure --prefix=${MUSL_PREFIX} --enable-static --disable-shared
        BUILD_COMMAND env REALGCC=/usr/bin/gcc make
        INSTALL_COMMAND make install
        DEPENDS zlib-musl
        BUILD_BYPRODUCTS ${MUSL_PREFIX}/lib/libsodium.a
    )


    # Build alsa-lib with musl
    ExternalProject_Add(alsa-lib-musl
        URL https://www.alsa-project.org/files/pub/lib/alsa-lib-1.2.12.tar.bz2
        URL_HASH SHA256=4868cd908627279da5a634f468701625be8cc251d84262c7e5b6a218391ad0d2
        PREFIX ${MUSL_PREFIX}
        CONFIGURE_COMMAND env CC=${MUSL_CC} REALGCC=/usr/bin/gcc CFLAGS=-fPIC <SOURCE_DIR>/configure --prefix=${MUSL_PREFIX} --enable-static --disable-shared --disable-maintainer-mode
        BUILD_COMMAND env REALGCC=/usr/bin/gcc make
        INSTALL_COMMAND make install
        DEPENDS libsodium-musl
        BUILD_BYPRODUCTS ${MUSL_PREFIX}/lib/libasound.a
    )

    # Build PortAudio with musl (depends on ALSA)
    ExternalProject_Add(portaudio-musl
        URL http://files.portaudio.com/archives/pa_stable_v190700_20210406.tgz
        URL_HASH SHA256=47efbf42c77c19a05d22e627d42873e991ec0c1357219c0d74ce6a2948cb2def
        PREFIX ${MUSL_PREFIX}
        CONFIGURE_COMMAND env CC=${MUSL_CC} REALGCC=/usr/bin/gcc CFLAGS=-fPIC PKG_CONFIG_PATH=${MUSL_PREFIX}/lib/pkgconfig <SOURCE_DIR>/configure --prefix=${MUSL_PREFIX} --enable-static --disable-shared --with-alsa --without-jack --without-oss
        BUILD_COMMAND env REALGCC=/usr/bin/gcc make
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${MUSL_PREFIX}/lib/libportaudio.a
        DEPENDS alsa-lib-musl
    )

    # Build libexecinfo for backtrace support with musl
    ExternalProject_Add(libexecinfo-musl
        GIT_REPOSITORY https://github.com/mikroskeem/libexecinfo.git
        GIT_TAG master
        PREFIX ${MUSL_PREFIX}
        CONFIGURE_COMMAND ""
        BUILD_COMMAND env CC=${MUSL_CC} REALGCC=/usr/bin/gcc CFLAGS=-fPIC make -C <SOURCE_DIR>
        INSTALL_COMMAND make -C <SOURCE_DIR> install PREFIX=${MUSL_PREFIX}
        BUILD_IN_SOURCE 1
        BUILD_BYPRODUCTS ${MUSL_PREFIX}/lib/libexecinfo.a
    )

    # Note: JACK is complex and optional - we'll skip it for static musl builds

    message(STATUS "Will build static dependencies to: ${MUSL_PREFIX}")
endif()

# =============================================================================
# mimalloc Memory Allocator (All Platforms)
# =============================================================================

# Note: USE_MIMALLOC option is defined early in the file (line ~559)
# so build type configuration can check it before adding DEBUG_MEMORY

if(USE_MIMALLOC)
    message(STATUS "Configuring mimalloc memory allocator...")

    # Always download and build mimalloc from GitHub
    message(STATUS "Downloading mimalloc from GitHub...")

    include(FetchContent)

    FetchContent_Declare(
        mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG v2.1.7  # Latest stable v2.x release
        GIT_SHALLOW TRUE
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

    FetchContent_MakeAvailable(mimalloc)

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

    message(STATUS "mimalloc will override malloc/free globally")
    message(STATUS "Your existing SAFE_MALLOC macros will automatically use mimalloc")
endif()

# =============================================================================
# Find Dependencies (matching Makefile pkg-config approach)
# =============================================================================

# Platform-specific package management and library linking
if(WIN32 AND NOT USE_MINGW)
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
elseif(WIN32 AND USE_MINGW)
    # MinGW on Windows - build with minimal dependencies for now
    message(STATUS "MinGW build - using minimal dependencies")

    # For MinGW, we'll build without external dependencies to keep it simple
    # This allows testing the core functionality without library complexity
    set(ZLIB_FOUND FALSE)
    set(ZLIB_LIBRARIES "")
    set(ZLIB_INCLUDE_DIRS "")

    set(LIBSODIUM_FOUND FALSE)
    set(LIBSODIUM_LIBRARIES "")
    set(LIBSODIUM_INCLUDE_DIRS "")

    set(PORTAUDIO_FOUND FALSE)
    set(PORTAUDIO_LIBRARIES "")
    set(PORTAUDIO_INCLUDE_DIRS "")

    message(STATUS "MinGW build configured without external dependencies")
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

    pkg_check_modules(ZLIB REQUIRED zlib)
    pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    pkg_check_modules(LIBSODIUM REQUIRED libsodium)

    # Test dependencies (matching TEST_PKG_CONFIG_LIBS in Makefile)
    if(BUILD_TESTS)
        pkg_check_modules(CRITERION criterion)
    endif()
endif()

# Windows SDK detection for native Clang builds
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT USE_MINGW)
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
# Find Windows-specific libraries (native mode only)
if(WIN32 AND NOT USE_MINGW)
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
elseif(WIN32 AND USE_MINGW)
    # MinGW uses different library names/linking
    set(WS2_32_LIB ws2_32)
    set(USER32_LIB user32)
    set(ADVAPI32_LIB advapi32)
    set(DBGHELP_LIB dbghelp)
    set(MF_LIB "")  # Media Foundation not used in MinGW
    set(MFPLAT_LIB "")
    set(MFREADWRITE_LIB "")
    set(MFUUID_LIB "")
    set(OLE32_LIB ole32)
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
    if(ZLIB_INCLUDE_DIRS)
        include_directories(${ZLIB_INCLUDE_DIRS})
    endif()
    if(LIBSODIUM_INCLUDE_DIRS)
        include_directories(${LIBSODIUM_INCLUDE_DIRS})
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
    endif()
endif()

# =============================================================================
# Source Files (matching Makefile structure)
# =============================================================================

# Common library sources (matching actual files in lib/)
set(LIB_COMMON_SRCS
    lib/common.c
    lib/logging.c
    lib/options.c
    lib/network.c
    lib/packet_queue.c
    lib/buffer_pool.c
    lib/hashtable.c
    lib/ringbuffer.c
    lib/video_frame.c
    lib/compression.c
    lib/crypto.c
    lib/crc32_hw.c
    lib/mixer.c
    lib/audio.c
    lib/image2ascii/image.c
    lib/image2ascii/output_buffer.c
    lib/aspect_ratio.c
    lib/image2ascii/ascii.c
    lib/ansi_fast.c
    lib/lock_debug.c
    lib/os/webcam.c
    lib/palette.c
    lib/utf8.c
)

# Platform-specific sources (matching Makefile platform detection)
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

# Combine all library sources
set(LIB_SRCS
    ${LIB_COMMON_SRCS}
    ${PLATFORM_SRCS}
    ${SIMD_SRCS}
)

# =============================================================================
# Create Library
# =============================================================================

add_library(ascii-chat-lib STATIC ${LIB_SRCS})

# Regenerate version header before building library
add_dependencies(ascii-chat-lib generate_version)

# Include generated version header
target_include_directories(ascii-chat-lib PUBLIC ${CMAKE_BINARY_DIR}/generated)

# Define the project source root for logging (so relative paths work regardless of repo name)
target_compile_definitions(ascii-chat-lib PRIVATE PROJECT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

# Define MI_DEBUG for our targets only (not mimalloc's build) to avoid macro redefinition warnings
if(DEFINED MIMALLOC_DEBUG_LEVEL)
    target_compile_definitions(ascii-chat-lib PRIVATE MI_DEBUG=${MIMALLOC_DEBUG_LEVEL})
endif()

# Define USE_MUSL flag for conditional compilation when using musl libc
if(USE_MUSL)
    target_compile_definitions(ascii-chat-lib PRIVATE USE_MUSL=1)
endif()

# Link library dependencies (matching Makefile linking order)
if(WIN32)
    target_link_libraries(ascii-chat-lib
        ${WS2_32_LIB}
        ${USER32_LIB}
        ${ADVAPI32_LIB}
        ${DBGHELP_LIB}
        ${MF_LIB}
        ${MFPLAT_LIB}
        ${MFREADWRITE_LIB}
        ${MFUUID_LIB}
        ${OLE32_LIB}
        ${PORTAUDIO_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${LIBSODIUM_LIBRARIES}
    )

    # Link mimalloc for Windows
    if(USE_MIMALLOC)
        target_link_libraries(ascii-chat-lib ${MIMALLOC_LIBRARIES})
    endif()
else()
    # Build LDFLAGS systematically (matches Makefile logic)
    set(PLATFORM_LIBS)

    if(PLATFORM_DARWIN)
        list(APPEND PLATFORM_LIBS
            ${FOUNDATION_FRAMEWORK}
            ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK}
            ${COREVIDEO_FRAMEWORK}
        )
    elseif(PLATFORM_LINUX)
        list(APPEND PLATFORM_LIBS
            ${CMAKE_THREAD_LIBS_INIT}
        )

        # Add JACK if available (matching Makefile)
        # Skip JACK for musl static builds - it's complex and optional
        if(JACK_FOUND AND NOT USE_MUSL)
            list(APPEND PLATFORM_LIBS ${JACK_LIBRARIES})
        endif()
    endif()

    # Check if pkg-config already provides pthread (matches Makefile)
    string(FIND "${ZLIB_LDFLAGS};${LIBSODIUM_LDFLAGS};${PORTAUDIO_LDFLAGS}" "-lpthread" PTHREAD_PROVIDED)
    if(PTHREAD_PROVIDED EQUAL -1)
        find_package(Threads REQUIRED)
        list(APPEND PLATFORM_LIBS ${CMAKE_THREAD_LIBS_INIT})
    endif()

    # Add unwind library if using Homebrew LLVM (to avoid duplicate -lunwind flags)
    if(DEFINED HOMEBREW_LLVM_LIBS)
        list(APPEND PLATFORM_LIBS ${HOMEBREW_LLVM_LIBS})
    endif()

    # When building with musl, don't add PortAudio to the library
    # It will be linked only to the client
    if(USE_MUSL)
        # Enable function/data sections for dead code elimination
        target_compile_options(ascii-chat-lib PRIVATE -ffunction-sections -fdata-sections)

        # Use musl-built static libraries (no audio libraries)
        target_link_libraries(ascii-chat-lib
            ${MUSL_PREFIX}/lib/libsodium.a
            ${MUSL_PREFIX}/lib/libz.a
            ${MUSL_PREFIX}/lib/libexecinfo.a
            m
            ${CMAKE_THREAD_LIBS_INIT}
        )
        if(USE_MIMALLOC)
            target_link_libraries(ascii-chat-lib ${MIMALLOC_LIBRARIES})
        endif()
        # Add dependency on libexecinfo
        add_dependencies(ascii-chat-lib libexecinfo-musl)
    else()
        # Standard glibc build includes all libraries
        target_link_libraries(ascii-chat-lib
            ${ZLIB_LIBRARIES}
            ${LIBSODIUM_LIBRARIES}
            ${PORTAUDIO_LIBRARIES}
            ${PLATFORM_LIBS}
            m  # math library
        )

        # Link mimalloc for Unix/macOS/Linux
        if(USE_MIMALLOC)
            target_link_libraries(ascii-chat-lib ${MIMALLOC_LIBRARIES})
        endif()
    endif()
endif()

# =============================================================================
# Precompiled Headers (Speed up compilation by 20-30%)
# =============================================================================

# CMake 3.16+ supports target_precompile_headers
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    option(USE_PRECOMPILED_HEADERS "Use precompiled headers for faster builds" ON)

    if(USE_PRECOMPILED_HEADERS)
        # Add most commonly included headers as precompiled
        # Based on analysis: these headers appear in 30+ source files
        #
        # NOTE: We CANNOT use precompiled headers with common.h because it defines
        # malloc/free macros that conflict with system headers. When a PCH includes
        # common.h, every file using the PCH tries to redefine malloc/free when
        # system headers like <malloc.h> are included, causing compiler errors.
        #
        # Instead, we precompile only safe headers that don't have macro conflicts.
        target_precompile_headers(ascii-chat-lib PRIVATE
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

# Server executable - now modular
add_executable(ascii-chat-server
    src/server/main.c
    src/server/client.c
    src/server/protocol.c
    src/server/stream.c
    src/server/render.c
    src/server/stats.c
)

# Server doesn't use audio drivers - enable dead code elimination
# This works with both GCC (musl) and Clang
target_compile_options(ascii-chat-server PRIVATE -ffunction-sections -fdata-sections)
if(NOT WIN32)
    if(APPLE)
        target_link_options(ascii-chat-server PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(ascii-chat-server PRIVATE -Wl,--gc-sections)
    endif()
endif()

target_link_libraries(ascii-chat-server ascii-chat-lib)

# Strip symbols in Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    find_program(STRIP_EXECUTABLE strip)
    if(STRIP_EXECUTABLE)
        add_custom_command(TARGET ascii-chat-server POST_BUILD
            COMMAND ${STRIP_EXECUTABLE} $<TARGET_FILE:ascii-chat-server>
            COMMENT "Stripping symbols from ascii-chat-server"
        )
    endif()
endif()

# Add musl dependency if building with musl
if(USE_MUSL)
    # Add dependencies on musl libraries (they'll build automatically)
    # Note: LTO optimization requires PortAudio to be linked even though server doesn't call it
    # The linker's dead code elimination will remove the unused audio code
    add_dependencies(ascii-chat-server libsodium-musl zlib-musl libexecinfo-musl portaudio-musl alsa-lib-musl)

    # Link against musl-built static libraries
    target_link_directories(ascii-chat-server PRIVATE ${MUSL_PREFIX}/lib)
    target_link_options(ascii-chat-server PRIVATE -static)

    # Link all libraries (LTO requires this, dead code elimination removes unused code)
    target_link_libraries(ascii-chat-server
        ${MUSL_PREFIX}/lib/libportaudio.a
        ${MUSL_PREFIX}/lib/libasound.a
        ${MUSL_PREFIX}/lib/libsodium.a
        ${MUSL_PREFIX}/lib/libz.a
        ${MUSL_PREFIX}/lib/libexecinfo.a
        -lm -lpthread
    )
endif()

# Client executable
add_executable(ascii-chat-client
    src/client/main.c
    src/client/server.c
    src/client/protocol.c
    src/client/display.c
    src/client/capture.c
    src/client/audio.c
    src/client/keepalive.c
)
target_link_libraries(ascii-chat-client ascii-chat-lib)

# Strip symbols in Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(STRIP_EXECUTABLE)
        add_custom_command(TARGET ascii-chat-client POST_BUILD
            COMMAND ${STRIP_EXECUTABLE} $<TARGET_FILE:ascii-chat-client>
            COMMENT "Stripping symbols from ascii-chat-client"
        )
    endif()
endif()

# Add musl dependency if building with musl
if(USE_MUSL)
    # Add dependencies on all musl libraries (they'll build automatically)
    add_dependencies(ascii-chat-client portaudio-musl alsa-lib-musl libsodium-musl zlib-musl libexecinfo-musl)

    # Link against musl-built static libraries
    target_link_directories(ascii-chat-client PRIVATE ${MUSL_PREFIX}/lib)
    target_link_options(ascii-chat-client PRIVATE -static)

    # Override system libraries with musl-built versions
    target_link_libraries(ascii-chat-client
        ${MUSL_PREFIX}/lib/libportaudio.a
        ${MUSL_PREFIX}/lib/libasound.a
        ${MUSL_PREFIX}/lib/libsodium.a
        ${MUSL_PREFIX}/lib/libz.a
        ${MUSL_PREFIX}/lib/libexecinfo.a
        -lm -lpthread
    )
endif()

# macOS Info.plist embedding (matches Makefile)
if(PLATFORM_DARWIN AND EXISTS "${CMAKE_SOURCE_DIR}/Info.plist")
    set_target_properties(ascii-chat-client PROPERTIES
        LINK_FLAGS "-sectcreate __TEXT __info_plist ${CMAKE_SOURCE_DIR}/Info.plist"
    )
endif()

# =============================================================================
# Tests (matching Makefile test infrastructure)
# =============================================================================

# Test framework setup
if(BUILD_TESTS AND CRITERION_FOUND AND NOT USE_MUSL)
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

        # Optional dependencies
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
            find_library(SSH2_LIB ssh2)
            find_library(HTTP_PARSER_LIB http_parser)
            find_library(PCRE2_LIB pcre2-8)

            if(SSH2_LIB)
                list(APPEND TEST_LDFLAGS ssh2)
            endif()
            if(HTTP_PARSER_LIB)
                list(APPEND TEST_LDFLAGS http_parser)
            endif()
            if(PCRE2_LIB)
                list(APPEND TEST_LDFLAGS pcre2-8)
            endif()
        endif()

        # GSSAPI/Kerberos support
        if(KRB5_GSSAPI_FOUND)
            list(APPEND TEST_LDFLAGS ${KRB5_GSSAPI_LIBRARIES})
        elseif(LIBSSH2_FOUND)
            list(APPEND TEST_LDFLAGS ${LIBSSH2_LIBRARIES})
        else()
            list(APPEND TEST_LDFLAGS gssapi_krb5 krb5 k5crypto com_err)
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
    foreach(test_src ${TEST_SRCS})
        # Transform test file paths to executable names with flattened structure
        # tests/unit/common_test.c -> test_unit_common
        # tests/integration/crypto_network_test.c -> test_integration_crypto_network
        get_filename_component(test_name ${test_src} NAME_WE)
        get_filename_component(test_dir ${test_src} DIRECTORY)
        get_filename_component(test_subdir ${test_dir} NAME)

        string(REPLACE "_test" "" test_base ${test_name})
        set(test_exe_name "test_${test_subdir}_${test_base}")

        # Add test executable with test utilities
        # - globals.c: Provides global symbols (g_should_exit) needed by lib code
        # - logging.c: Provides test-specific logging utilities (stdout/stderr redirection)
        add_executable(${test_exe_name} ${test_src} lib/tests/globals.c lib/tests/logging.c)

        # Disable precompiled headers for test targets to avoid conflicts with Criterion macros
        set_target_properties(${test_exe_name} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

        # Add Criterion include directories
        target_include_directories(${test_exe_name} PRIVATE ${CRITERION_INCLUDE_DIRS})

        # Link test dependencies (order matters for linking)
        target_link_libraries(${test_exe_name}
            ascii-chat-lib
            ${TEST_LDFLAGS}
        )

        # Use release objects for performance tests
        if(test_subdir STREQUAL "performance")
            target_compile_options(${test_exe_name} PRIVATE -O3 -DNDEBUG)
        endif()

        # Add to CTest
        add_test(NAME ${test_exe_name} COMMAND ${test_exe_name})
    endforeach()

    # Custom targets for different test modes (matches Makefile)
    add_custom_target(test_debug
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running tests in debug mode"
    )

    add_custom_target(test_release
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C Release
        COMMENT "Running tests in release mode"
    )

    # Overall test target
    add_custom_target(test_all
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running all tests"
    )

else()
    message(WARNING "Criterion testing framework not found. Tests will not be built.")
endif()

# =============================================================================
# Installation
# =============================================================================

install(TARGETS ascii-chat-server ascii-chat-client
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
            ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ascii-chat-server ascii-chat-client
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
message(STATUS "  Run server:          ./build/bin/ascii-chat-server")
message(STATUS "  Run client:          ./build/bin/ascii-chat-client")
if(CRITERION_FOUND)
    message(STATUS "  Run tests:           ./tests/scripts/run_tests.sh")
    message(STATUS "  Run tests (CMake):   cmake --build build --target test_all")
endif()
message(STATUS "  Format code:         cmake --build build --target format")
message(STATUS "  Check formatting:    cmake --build build --target format-check")
message(STATUS "  Install deps:        cmake --build build --target deps")
message(STATUS "========================================")
message(STATUS "")
