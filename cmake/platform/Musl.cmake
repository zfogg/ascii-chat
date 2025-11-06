# =============================================================================
# musl libc Configuration Module
# =============================================================================
# This module handles musl libc configuration for Linux builds, providing
# both pre-project() compiler setup and post-project() linker configuration.
#
# Functions:
#   - configure_musl_pre_project(): Sets up compiler before project()
#   - configure_musl_post_project(): Sets up linker flags after project()
#
# Prerequisites:
#   - configure_musl_pre_project() must run before project()
#   - configure_musl_post_project() must run after project()
# =============================================================================

# =============================================================================
# Part 1: Pre-project() Configuration (Compiler Setup)
# =============================================================================
# This section must run before project() to set CMAKE_C_COMPILER
# =============================================================================

function(configure_musl_pre_project)
    # Option to use musl libc (Linux only - musl is not compatible with macOS or Windows)
    # =============================================================================
    # musl libc Configuration for Linux Release Builds
    # =============================================================================
    # musl + mimalloc provides ~85% better performance than glibc on Release builds
    # Performance results (Release builds, test-pattern mode):
    #   - glibc + mimalloc:  ~60 FPS
    #   - musl + mimalloc:   ~100 FPS (85% faster!)
    #   - musl only:         ~55 FPS
    #
    # Default behavior by platform and build type:
    #   Linux:
    #     - Release/RelWithDebInfo: ON (musl + mimalloc for best performance)
    #     - Debug/Dev: OFF (use glibc for better debugging experience)
    #   macOS/Windows: OFF (not applicable - use system libc + mimalloc)
    #
    # Detect Linux early (before project()) using CMAKE_HOST_SYSTEM_NAME
    # CMAKE_SYSTEM_NAME is only set after project() is called
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
            option(USE_MUSL "Use musl libc + mimalloc for optimal performance (Linux Release builds)" ON)
        else()
            option(USE_MUSL "Use musl libc instead of glibc (Linux only)" OFF)
        endif()
    else()
        # musl is Linux-only
        set(USE_MUSL OFF CACHE BOOL "musl is Linux-only (not ${CMAKE_HOST_SYSTEM_NAME})" FORCE)
    endif()

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

        find_program(CLANGXX_COMPILER clang++)
        if(NOT CLANGXX_COMPILER)
            message(FATAL_ERROR "clang++ not found. Install clang for musl static-PIE support:\n"
                                "  Ubuntu/Debian: sudo apt install clang")
        endif()

        # Verify musl development files are installed
        # Check multiple locations (Arch vs Debian/Ubuntu)
        if(EXISTS "/usr/lib/musl/lib/libc.a")
            # Arch Linux location
            set(MUSL_LIBC_PATH "/usr/lib/musl/lib/libc.a")
        elseif(EXISTS "/usr/lib/x86_64-linux-musl/libc.a")
            # Debian/Ubuntu location
            set(MUSL_LIBC_PATH "/usr/lib/x86_64-linux-musl/libc.a")
        elseif(EXISTS "/usr/x86_64-linux-musl/lib64/libc.a")
            # Fedora location
            set(MUSL_LIBC_PATH "/usr/x86_64-linux-musl/lib64/libc.a")
        else()
            message(FATAL_ERROR "musl development files not found. Install musl-dev:\n"
                                "  Arch Linux: sudo pacman -S musl\n"
                                "  Ubuntu/Debian: sudo apt install musl-tools musl-dev\n"
                                "  Fedora: sudo dnf install musl-devel")
        endif()

        # Set compiler to clang BEFORE project()
        set(CMAKE_C_COMPILER "${CLANG_COMPILER}" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "${CLANGXX_COMPILER}" CACHE FILEPATH "CXX compiler" FORCE)

        # Set LLVM archiver and ranlib for proper archive handling
        # Use helper function from LLVM.cmake
        include(${CMAKE_SOURCE_DIR}/cmake/compiler/LLVM.cmake)
        find_llvm_tools()

        message(STATUS "Using ${BoldCyan}clang${ColorReset} with ${BoldYellow}musl${ColorReset} for static-PIE support")
    endif()
endfunction()

# =============================================================================
# Part 2: Post-project() Configuration (Linker Setup)
# =============================================================================
# This section must run after project() to configure linker flags
# =============================================================================

function(configure_musl_post_project)
    if(NOT USE_MUSL)
        return()
    endif()

    # Set musl dependency cache directory for ExternalProject builds
    # This must be done here (after USE_MUSL is defined) rather than in Init.cmake
    set(MUSL_DEPS_DIR_STATIC "${DEPS_CACHE_MUSL}" CACHE PATH "Musl-specific dependencies cache for static builds" FORCE)
    message(STATUS "${BoldBlue}Musl${ColorReset} dependency cache directory: ${BoldMagenta}${MUSL_DEPS_DIR_STATIC}${ColorReset}")

    # Use -static-pie flag to build fully static PIE binaries with musl
    # Combines static linking with Position Independent Executable for security (ASLR)
    # Using clang directly with musl for proper static-PIE support (musl-gcc wrapper doesn't support it)
    # See: https://github.com/rust-lang/rust/issues/95926

    # Find GCC library directory for libgcc.a
    # Use gcc -print-libgcc-file-name to get the actual path (works on all distros)
    execute_process(
        COMMAND gcc -print-libgcc-file-name
        OUTPUT_VARIABLE GCC_LIBGCC_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    get_filename_component(GCC_LIBDIR "${GCC_LIBGCC_PATH}" DIRECTORY)

    # Detect musl library directory (Arch vs Debian/Ubuntu vs Fedora)
    if(EXISTS "/usr/lib/musl/lib")
        # Arch Linux location
        set(MUSL_LIBDIR "/usr/lib/musl/lib")
    elseif(EXISTS "/usr/x86_64-linux-musl/lib64")
        # Fedora location
        set(MUSL_LIBDIR "/usr/x86_64-linux-musl/lib64")
    else()
        # Debian/Ubuntu location
        set(MUSL_LIBDIR "/usr/lib/x86_64-linux-musl")
    endif()

    # Configure clang to use musl with static-PIE
    # Based on: https://wiki.debian.org/musl
    # Note: -fPIE is added by CompilerFlags.cmake for all PIE builds (dynamic and static)
    add_compile_options(
        -target x86_64-linux-musl
        -DUSE_MUSL
    )

    # Linker flags for static-PIE linking with musl
    # Use rcrt1.o for static-PIE (combines static linking with ASLR security)
    # Use lld for LTO support (lld has built-in LTO, doesn't need LLVMgold.so plugin)
    # Force retention of custom sections with -Wl,--undefined to prevent LTO from removing them
    set(CMAKE_EXE_LINKER_FLAGS
        "-target x86_64-linux-musl -fuse-ld=lld -static-pie -nostdlib -L${MUSL_LIBDIR} ${MUSL_LIBDIR}/rcrt1.o ${MUSL_LIBDIR}/crti.o -Wl,--undefined=ascii_chat_custom_section -Wl,--undefined=ascii_chat_comment_string -Wl,--undefined=ascii_chat_version_string"
        CACHE STRING "Linker flags for musl static-PIE linking" FORCE
    )

    # Shared library linker flags for musl builds
    # Note: Shared libraries link against glibc (not musl) since they'll be used on glibc systems
    # Don't use -target x86_64-linux-musl for shared libs - let it use default glibc
    set(CMAKE_SHARED_LINKER_FLAGS
        "-fuse-ld=lld -flto -Wl,--gc-sections"
        CACHE STRING "Shared linker flags for musl builds (uses glibc for compatibility)" FORCE
    )

    # Link with musl libc and libgcc at the end
    link_libraries(c "${GCC_LIBDIR}/libgcc.a")
    add_link_options("${MUSL_LIBDIR}/crtn.o")

    # Disable precompiled headers
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "Disable PCH for musl" FORCE)

    message(STATUS "${BoldBlue}musl${ColorReset} C library enabled (static linking with ${BoldCyan}clang${ColorReset})")
    message(STATUS "Using ${BoldCyan}GCC ${GCC_VERSION}${ColorReset} libgcc from: ${BoldMagenta}${GCC_LIBDIR}${ColorReset}")
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
    message(STATUS "Using compiler for ${BoldBlue}musl${ColorReset} dependencies: ${BoldCyan}${MUSL_CC}${ColorReset}")

    # Note: zstd-musl, libsodium-musl, alsa-lib-musl, portaudio-musl, and libexecinfo-musl are now built in MuslDependencies.cmake
endfunction()

