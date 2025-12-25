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
        # Detect architecture early using uname (before project() sets CMAKE_SYSTEM_PROCESSOR)
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE HOST_ARCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        # Only enable musl by default on x86_64 - ARM64 musl support on GitHub runners is limited
        # Autotools (autoconf, automake, libtool) are installed by scripts/install-deps.sh before cmake runs
        if(HOST_ARCH STREQUAL "x86_64" AND (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
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
        # Use centralized clang from FindPrograms.cmake
        if(NOT ASCIICHAT_CLANG_EXECUTABLE)
            message(FATAL_ERROR "clang not found. Install clang for musl static-PIE support:\n"
                                "  Ubuntu/Debian: sudo apt install clang")
        endif()
        set(CLANG_COMPILER "${ASCIICHAT_CLANG_EXECUTABLE}")

        if(NOT ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE)
            message(FATAL_ERROR "clang++ not found. Install clang for musl static-PIE support:\n"
                                "  Ubuntu/Debian: sudo apt install clang")
        endif()
        set(CLANGXX_COMPILER "${ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE}")

        # Verify musl development files are installed
        # Check multiple locations (Arch vs Debian/Ubuntu) and architectures (x86_64 vs aarch64)
        if(EXISTS "/usr/lib/musl/lib/libc.a")
            # Arch Linux location (all architectures)
            set(MUSL_LIBC_PATH "/usr/lib/musl/lib/libc.a")
        elseif(EXISTS "/usr/lib/x86_64-linux-musl/libc.a")
            # Debian/Ubuntu x86_64 location
            set(MUSL_LIBC_PATH "/usr/lib/x86_64-linux-musl/libc.a")
        elseif(EXISTS "/usr/lib/aarch64-linux-musl/libc.a")
            # Debian/Ubuntu ARM64/aarch64 location
            set(MUSL_LIBC_PATH "/usr/lib/aarch64-linux-musl/libc.a")
        elseif(EXISTS "/usr/x86_64-linux-musl/lib64/libc.a")
            # Fedora x86_64 location
            set(MUSL_LIBC_PATH "/usr/x86_64-linux-musl/lib64/libc.a")
        elseif(EXISTS "/usr/aarch64-linux-musl/lib64/libc.a")
            # Fedora ARM64/aarch64 location
            set(MUSL_LIBC_PATH "/usr/aarch64-linux-musl/lib64/libc.a")
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

    # Set musl dependency cache directories for ExternalProject builds
    # ASCIICHAT_DEPS_CACHE_MUSL is NOT set in CMakeLists.txt because USE_MUSL
    # isn't defined until Musl.cmake is included (after the cache dir setup)
    # So we set it here to ensure it's properly configured

    # First validate that ASCIICHAT_DEPS_CACHE_DIR is properly set
    if(NOT ASCIICHAT_DEPS_CACHE_DIR OR ASCIICHAT_DEPS_CACHE_DIR STREQUAL "" OR ASCIICHAT_DEPS_CACHE_DIR MATCHES "^/[^/]*$")
        message(FATAL_ERROR "ASCIICHAT_DEPS_CACHE_DIR is invalid for musl builds: '${ASCIICHAT_DEPS_CACHE_DIR}'\n"
                            "This would create directories at root /. Please report this bug.")
    endif()

    if(NOT ASCIICHAT_DEPS_CACHE_MUSL OR ASCIICHAT_DEPS_CACHE_MUSL STREQUAL "")
        set(ASCIICHAT_DEPS_CACHE_MUSL "${ASCIICHAT_DEPS_CACHE_DIR}/musl" CACHE PATH "Dependency cache for musl builds" FORCE)
    endif()

    # Validate musl cache dir
    if(ASCIICHAT_DEPS_CACHE_MUSL MATCHES "^/[^/]*$" OR ASCIICHAT_DEPS_CACHE_MUSL STREQUAL "/musl")
        message(FATAL_ERROR "ASCIICHAT_DEPS_CACHE_MUSL is invalid: '${ASCIICHAT_DEPS_CACHE_MUSL}'\n"
                            "ASCIICHAT_DEPS_CACHE_DIR: '${ASCIICHAT_DEPS_CACHE_DIR}'\n"
                            "This would create directories at root /. Please report this bug.")
    endif()

    set(MUSL_DEPS_DIR_STATIC "${ASCIICHAT_DEPS_CACHE_MUSL}" CACHE PATH "Musl-specific dependencies cache for static builds" FORCE)
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

    # Detect architecture for musl target triple
    # CMAKE_SYSTEM_PROCESSOR is available after project()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
        set(MUSL_ARCH "aarch64")
    else()
        set(MUSL_ARCH "x86_64")
    endif()
    set(MUSL_TARGET "${MUSL_ARCH}-linux-musl")

    # Detect musl library directory (Arch vs Debian/Ubuntu vs Fedora)
    # Check architecture-specific paths
    if(EXISTS "/usr/lib/musl/lib")
        # Arch Linux location (all architectures)
        set(MUSL_LIBDIR "/usr/lib/musl/lib")
    elseif(EXISTS "/usr/${MUSL_ARCH}-linux-musl/lib64")
        # Fedora location
        set(MUSL_LIBDIR "/usr/${MUSL_ARCH}-linux-musl/lib64")
    elseif(EXISTS "/usr/lib/${MUSL_ARCH}-linux-musl")
        # Debian/Ubuntu location
        set(MUSL_LIBDIR "/usr/lib/${MUSL_ARCH}-linux-musl")
    else()
        message(FATAL_ERROR "musl library directory not found for ${MUSL_ARCH}. Install musl-dev:\n"
                            "  Arch Linux: sudo pacman -S musl\n"
                            "  Ubuntu/Debian: sudo apt install musl-tools musl-dev\n"
                            "  Fedora: sudo dnf install musl-devel")
    endif()

    # Configure clang to use musl with static-PIE
    # Based on: https://wiki.debian.org/musl
    # Note: -fPIE is added by CompilerFlags.cmake for all PIE builds (dynamic and static)
    # Use LLVM runtime libraries instead of GCC for musl compatibility
    # Disable FORTIFY_SOURCE: musl doesn't provide __*_chk wrapper functions (glibc-specific)
    add_compile_options(
        -target ${MUSL_TARGET}
        -DUSE_MUSL
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=0
    )
    # Use libc++ for C++ code (musl-compatible, unlike libstdc++)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++" CACHE STRING "C++ flags for musl" FORCE)

    # Linker flags for static-PIE linking with musl
    # Use rcrt1.o for static-PIE (combines static linking with ASLR security)
    # Use lld for LTO support (lld has built-in LTO, doesn't need LLVMgold.so plugin)
    # Force retention of custom sections with -Wl,--undefined to prevent LTO from removing them
    # Apply RELRO hardening: -Wl,-z,relro enables GOT read-only relocation, -Wl,-z,now enables full RELRO (BIND_NOW)
    set(CMAKE_EXE_LINKER_FLAGS
        "-target ${MUSL_TARGET} -fuse-ld=lld -static-pie -nostdlib -L${MUSL_LIBDIR} ${MUSL_LIBDIR}/rcrt1.o ${MUSL_LIBDIR}/crti.o -Wl,--undefined=ascii_chat_custom_section -Wl,--undefined=ascii_chat_comment_string -Wl,--undefined=ascii_chat_version_string -Wl,-z,relro -Wl,-z,now"
        CACHE STRING "Linker flags for musl static-PIE linking" FORCE
    )

    # Shared library linker flags for musl builds
    # Note: Shared libraries link against glibc (not musl) since they'll be used on glibc systems
    # Don't use musl target for shared libs - let it use default glibc
    set(CMAKE_SHARED_LINKER_FLAGS
        "-fuse-ld=lld -flto -Wl,--gc-sections"
        CACHE STRING "Shared linker flags for musl builds (uses glibc for compatibility)" FORCE
    )

    # Detect LLVM runtime libraries location
    # Check git-compiled LLVM first (all three libs together), then system packages
    if(EXISTS "/usr/local/lib/x86_64-unknown-linux-gnu/libc++.a" AND
       EXISTS "/usr/local/lib/x86_64-unknown-linux-gnu/libc++abi.a" AND
       EXISTS "/usr/local/lib/x86_64-unknown-linux-gnu/libunwind.a")
        set(LIBCXX_PATH "/usr/local/lib/x86_64-unknown-linux-gnu/libc++.a")
        set(LIBCXXABI_PATH "/usr/local/lib/x86_64-unknown-linux-gnu/libc++abi.a")
        set(LIBUNWIND_PATH "/usr/local/lib/x86_64-unknown-linux-gnu/libunwind.a")
        set(LLVM_RUNTIME_SOURCE "local LLVM installation")
    elseif(EXISTS "/usr/lib/libc++.a" AND EXISTS "/usr/lib/libc++abi.a")
        set(LIBCXX_PATH "/usr/lib/libc++.a")
        set(LIBCXXABI_PATH "/usr/lib/libc++abi.a")
        # System libunwind might not exist - not required for basic C++ support
        if(EXISTS "/usr/lib/libunwind.a")
            set(LIBUNWIND_PATH "/usr/lib/libunwind.a")
        else()
            # Try to find libunwind from packages
            find_library(LIBUNWIND_PATH NAMES unwind)
            if(NOT LIBUNWIND_PATH)
                message(WARNING "libunwind.a not found - exception handling may not work")
            endif()
        endif()
        set(LLVM_RUNTIME_SOURCE "system packages")
    else()
        message(FATAL_ERROR "LLVM runtime libraries not found. Install: sudo pacman -S libc++ libc++abi")
    endif()
    message(STATUS "Using LLVM runtime libraries (${LLVM_RUNTIME_SOURCE}):")
    message(STATUS "  libc++: ${LIBCXX_PATH}")
    message(STATUS "  libc++abi: ${LIBCXXABI_PATH}")
    if(LIBUNWIND_PATH)
        message(STATUS "  libunwind: ${LIBUNWIND_PATH}")
    endif()

    # Link with musl libc and LLVM runtime libraries
    # Use compiler-rt instead of libgcc for clang compatibility
    # Use libc++ instead of libstdc++ for musl compatibility
    # libunwind provides C++ exception handling symbols (_Unwind_*)
    # Must link libraries explicitly for static musl builds with full paths to avoid duplicates
    link_libraries(
        c
        ${LIBCXX_PATH}
        ${LIBCXXABI_PATH}
    )
    if(LIBUNWIND_PATH)
        link_libraries(${LIBUNWIND_PATH})
    endif()
    add_link_options(
        "${MUSL_LIBDIR}/crtn.o"
    )

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

