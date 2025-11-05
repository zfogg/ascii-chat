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
        if(NOT EXISTS "/usr/lib/x86_64-linux-musl/libc.a")
            message(FATAL_ERROR "musl development files not found. Install musl-dev:\n"
                                "  Arch Linux: sudo pacman -S musl\n"
                                "  Ubuntu/Debian: sudo apt install musl-tools musl-dev")
        endif()

        # Set compiler to clang BEFORE project()
        set(CMAKE_C_COMPILER "${CLANG_COMPILER}" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "${CLANGXX_COMPILER}" CACHE FILEPATH "CXX compiler" FORCE)

        # Set LLVM archiver and ranlib for proper archive handling
        # Use helper function from LLVM.cmake
        include(${CMAKE_SOURCE_DIR}/cmake/LLVM.cmake)
        find_llvm_tools()

        message(STATUS "Using clang with musl for static-PIE support")
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
    # Note: -fPIE is added by CompilerFlags.cmake for all PIE builds (dynamic and static)
    add_compile_options(
        -target x86_64-linux-musl
    )

    # Linker flags for static-PIE linking with musl
    # Use rcrt1.o for static-PIE (combines static linking with ASLR security)
    # Use lld for LTO support (lld has built-in LTO, doesn't need LLVMgold.so plugin)
    # Force retention of custom sections with -Wl,--undefined to prevent LTO from removing them
    set(CMAKE_EXE_LINKER_FLAGS
        "-target x86_64-linux-musl -fuse-ld=lld -static-pie -nostdlib -L${MUSL_LIBDIR} ${MUSL_LIBDIR}/rcrt1.o ${MUSL_LIBDIR}/crti.o -Wl,--undefined=ascii_chat_custom_section"
        CACHE STRING "Linker flags for musl static-PIE linking" FORCE
    )
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" CACHE STRING "Shared linker flags for musl" FORCE)

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

    # Note: zstd-musl, libsodium-musl, alsa-lib-musl, portaudio-musl, and libexecinfo-musl are now built in MuslDependencies.cmake
endfunction()

