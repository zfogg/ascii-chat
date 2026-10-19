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

function(configure_musl_pre_project BUILD_TYPE_PARAM)
    if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(USE_MUSL OFF CACHE BOOL "musl is Linux-only" FORCE)
        return()
    endif()
    set(_default OFF)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE _host_arch OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_host_arch STREQUAL "x86_64" AND BUILD_TYPE_PARAM MATCHES "^(Release|RelWithDebInfo)$")
        set(_default ON)
    endif()
    option(USE_MUSL "Build against musl libc with Clang" ${_default})
    if(USE_MUSL AND NOT CMAKE_TOOLCHAIN_FILE)
        get_filename_component(_musl_toolchain "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../toolchains/Musl.cmake" ABSOLUTE)
        set(CMAKE_TOOLCHAIN_FILE "${_musl_toolchain}" CACHE FILEPATH "Clang musl toolchain" FORCE)
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
    if(NOT GCC_LIBGCC_PATH OR NOT EXISTS "${GCC_LIBGCC_PATH}")
        message(FATAL_ERROR "GCC libgcc runtime was not found; it is required for musl static linking")
    endif()
    set(MUSL_LIBGCC_PATH "${GCC_LIBGCC_PATH}" CACHE FILEPATH "Compiler runtime archive for musl static executables" FORCE)

    if(NOT CMAKE_C_COMPILER_TARGET STREQUAL MUSL_TARGET)
        message(FATAL_ERROR "The musl compiler toolchain was not loaded. Configure a fresh build tree with cmake --fresh.")
    endif()
    target_compile_definitions(asciichat-project-options INTERFACE USE_MUSL _FORTIFY_SOURCE=0)
    target_compile_options(asciichat-project-options INTERFACE -U_FORTIFY_SOURCE)

    # Compiler target, CRT startup files and search boundaries belong to the toolchain.
    # Retention and hardening are policy for our own binaries and libraries.
    target_link_options(asciichat-project-options INTERFACE
        "LINKER:--undefined=ascii_chat_custom_section"
        "LINKER:--undefined=ascii_chat_comment_string"
        "LINKER:--undefined=ascii_chat_version_string"
        "LINKER:-z,relro" "LINKER:-z,now")

    # Detect LLVM runtime libraries location
    # For musl builds, we need libc++ compiled WITHOUT glibc fortify functions.
    # Ubuntu's libc++ is compiled with -D_FORTIFY_SOURCE=2, causing runtime issues.
    #
    # Solution: Download pre-built libc++ from Alpine Linux (uses musl natively)
    set(ALPINE_LIBCXX_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/alpine-libcxx")
    set(ALPINE_VERSION "3.21")  # Alpine Linux version
    set(ALPINE_MIRROR "https://dl-cdn.alpinelinux.org/alpine")

    # Map CMake architecture to Alpine package architecture
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        set(ALPINE_ARCH "x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(ALPINE_ARCH "aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(armv7|armhf)$")
        set(ALPINE_ARCH "armv7")
    else()
        message(FATAL_ERROR "Unsupported architecture for Alpine libc++: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(ALPINE_ARCH STREQUAL "aarch64")
        # Use Alpine's compiler runtime for AArch64 musl. The host Debian
        # libgcc archive contains an LSE initializer that calls glibc-only
        # __getauxval; Alpine's archive provides the same helpers against musl.
        set(ALPINE_GCC_VERSION "14.2.0-r4")
        set(ALPINE_GCC_RUNTIME_VERSION "14.2.0")
        set(ALPINE_GCC_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/alpine-gcc")
        set(ALPINE_GCC_PACKAGE "gcc-${ALPINE_GCC_VERSION}.apk")
        set(ALPINE_LIBGCC_PATH
            "${ALPINE_GCC_DIR}/usr/lib/gcc/aarch64-alpine-linux-musl/${ALPINE_GCC_RUNTIME_VERSION}/libgcc.a")

        if(NOT EXISTS "${ALPINE_LIBGCC_PATH}")
            file(MAKE_DIRECTORY "${ALPINE_GCC_DIR}")
            file(DOWNLOAD
                "${ALPINE_MIRROR}/v${ALPINE_VERSION}/main/${ALPINE_ARCH}/${ALPINE_GCC_PACKAGE}"
                "${ALPINE_GCC_DIR}/${ALPINE_GCC_PACKAGE}"
                EXPECTED_HASH SHA256=f9fa19072d1f1a4f72631f3fd090a85a48b95fa08b2062630ea96813275e130f
                STATUS ALPINE_GCC_DOWNLOAD_STATUS
                SHOW_PROGRESS
            )
            list(GET ALPINE_GCC_DOWNLOAD_STATUS 0 ALPINE_GCC_DOWNLOAD_CODE)
            if(NOT ALPINE_GCC_DOWNLOAD_CODE EQUAL 0)
                list(GET ALPINE_GCC_DOWNLOAD_STATUS 1 ALPINE_GCC_DOWNLOAD_ERROR)
                message(FATAL_ERROR "Failed to download Alpine AArch64 libgcc: ${ALPINE_GCC_DOWNLOAD_ERROR}")
            endif()
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${ALPINE_GCC_PACKAGE}"
                WORKING_DIRECTORY "${ALPINE_GCC_DIR}"
                RESULT_VARIABLE ALPINE_GCC_EXTRACT_RESULT
            )
            if(NOT ALPINE_GCC_EXTRACT_RESULT EQUAL 0 OR NOT EXISTS "${ALPINE_LIBGCC_PATH}")
                message(FATAL_ERROR "Failed to extract Alpine AArch64 libgcc from ${ALPINE_GCC_PACKAGE}")
            endif()
        endif()

        set(MUSL_LIBGCC_PATH "${ALPINE_LIBGCC_PATH}" CACHE FILEPATH
            "Musl-compatible AArch64 compiler runtime archive" FORCE)
        message(STATUS "Using Alpine musl AArch64 compiler runtime: ${MUSL_LIBGCC_PATH}")
    endif()

    if(NOT EXISTS "${ALPINE_LIBCXX_DIR}/libc++.a")
        message(STATUS "Downloading musl-compatible libc++ from Alpine Linux...")
        file(MAKE_DIRECTORY "${ALPINE_LIBCXX_DIR}")

        # Download libc++ static package from Alpine
        set(LIBCXX_STATIC_PKG "libc++-static-19.1.4-r1.apk")
        set(LIBCXX_STATIC_URL "${ALPINE_MIRROR}/v${ALPINE_VERSION}/main/${ALPINE_ARCH}/${LIBCXX_STATIC_PKG}")

        message(STATUS "  Downloading ${LIBCXX_STATIC_URL}")
        file(DOWNLOAD "${LIBCXX_STATIC_URL}"
             "${ALPINE_LIBCXX_DIR}/${LIBCXX_STATIC_PKG}"
             STATUS DOWNLOAD_STATUS
             SHOW_PROGRESS)

        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
            message(FATAL_ERROR "Failed to download Alpine libc++: ${ERROR_MSG}")
        endif()

        # Download llvm-libunwind-static package from Alpine (required for exception handling)
        set(LIBUNWIND_STATIC_PKG "llvm-libunwind-static-19.1.4-r1.apk")
        set(LIBUNWIND_STATIC_URL "${ALPINE_MIRROR}/v${ALPINE_VERSION}/main/${ALPINE_ARCH}/${LIBUNWIND_STATIC_PKG}")

        message(STATUS "  Downloading ${LIBUNWIND_STATIC_URL}")
        file(DOWNLOAD "${LIBUNWIND_STATIC_URL}"
             "${ALPINE_LIBCXX_DIR}/${LIBUNWIND_STATIC_PKG}"
             STATUS DOWNLOAD_STATUS
             SHOW_PROGRESS)

        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
            message(FATAL_ERROR "Failed to download Alpine libunwind: ${ERROR_MSG}")
        endif()

        # Download libc++-dev package from Alpine (contains headers for C++ compilation)
        # This is CRITICAL: C++ code must be compiled with matching headers and libraries
        # to avoid ABI mismatches (e.g., pthread_cond_clockwait, __hash_memory)
        set(LIBCXX_DEV_PKG "libc++-dev-19.1.4-r1.apk")
        set(LIBCXX_DEV_URL "${ALPINE_MIRROR}/v${ALPINE_VERSION}/main/${ALPINE_ARCH}/${LIBCXX_DEV_PKG}")

        message(STATUS "  Downloading ${LIBCXX_DEV_URL}")
        file(DOWNLOAD "${LIBCXX_DEV_URL}"
             "${ALPINE_LIBCXX_DIR}/${LIBCXX_DEV_PKG}"
             STATUS DOWNLOAD_STATUS
             SHOW_PROGRESS)

        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
            message(FATAL_ERROR "Failed to download Alpine libc++-dev: ${ERROR_MSG}")
        endif()

        # Extract .apk files (they're tar.gz archives)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${LIBCXX_STATIC_PKG}"
            WORKING_DIRECTORY "${ALPINE_LIBCXX_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract Alpine libc++ package")
        endif()

        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${LIBUNWIND_STATIC_PKG}"
            WORKING_DIRECTORY "${ALPINE_LIBCXX_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract Alpine libunwind package")
        endif()

        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${LIBCXX_DEV_PKG}"
            WORKING_DIRECTORY "${ALPINE_LIBCXX_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract Alpine libc++-dev package")
        endif()

        message(STATUS "  Extracted Alpine libc++, libunwind, and headers to ${ALPINE_LIBCXX_DIR}")
    endif()

    # Check if Alpine libs were successfully extracted
    if(EXISTS "${ALPINE_LIBCXX_DIR}/usr/lib/libc++.a" AND
       EXISTS "${ALPINE_LIBCXX_DIR}/usr/lib/libc++abi.a")
        set(LIBCXX_PATH "${ALPINE_LIBCXX_DIR}/usr/lib/libc++.a")
        set(LIBCXXABI_PATH "${ALPINE_LIBCXX_DIR}/usr/lib/libc++abi.a")
        if(EXISTS "${ALPINE_LIBCXX_DIR}/usr/lib/libunwind.a")
            set(LIBUNWIND_PATH "${ALPINE_LIBCXX_DIR}/usr/lib/libunwind.a")
        endif()
        # Set include path for libc++ headers (from libc++-dev package)
        if(EXISTS "${ALPINE_LIBCXX_DIR}/usr/include/c++/v1")
            set(ALPINE_LIBCXX_INCLUDE_DIR "${ALPINE_LIBCXX_DIR}/usr/include/c++/v1" CACHE PATH "Alpine libc++ headers")
            message(STATUS "  Alpine libc++ headers: ${ALPINE_LIBCXX_INCLUDE_DIR}")
        endif()
        set(LLVM_RUNTIME_SOURCE "Alpine Linux (musl-native)")
        set(LIBCXX_FOUND TRUE)
    else()
        set(LIBCXX_FOUND FALSE)
    endif()

    # Fallback: Search for system libc++ (may have fortify issues)
    if(NOT LIBCXX_FOUND)
        message(WARNING "Alpine libc++ download failed, falling back to system libraries")
        # Check multiple possible installation paths in priority order:
        # 1. Git-compiled LLVM in /usr/local (all three libs together)
        # 2. System packages in /usr/lib (unversioned)
        # 3. LLVM-versioned packages in /usr/lib/llvm-*/lib
        # 4. Debian/Ubuntu multiarch in /usr/lib/x86_64-linux-gnu
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
        # Try to find versioned LLVM packages (e.g., libc++-21-dev on Ubuntu)
        # Check /usr/lib/llvm-*/lib for LLVM package installations
        file(GLOB LLVM_LIB_DIRS "/usr/lib/llvm-*/lib")
        list(SORT LLVM_LIB_DIRS)
        list(REVERSE LLVM_LIB_DIRS)  # Prefer higher versions

        set(LIBCXX_FOUND FALSE)
        foreach(LLVM_DIR ${LLVM_LIB_DIRS})
            if(EXISTS "${LLVM_DIR}/libc++.a" AND EXISTS "${LLVM_DIR}/libc++abi.a")
                set(LIBCXX_PATH "${LLVM_DIR}/libc++.a")
                set(LIBCXXABI_PATH "${LLVM_DIR}/libc++abi.a")
                if(EXISTS "${LLVM_DIR}/libunwind.a")
                    set(LIBUNWIND_PATH "${LLVM_DIR}/libunwind.a")
                endif()
                set(LLVM_RUNTIME_SOURCE "LLVM versioned packages (${LLVM_DIR})")
                set(LIBCXX_FOUND TRUE)
                break()
            endif()
        endforeach()

        # If not found in LLVM dirs, try multiarch paths (Debian/Ubuntu)
        if(NOT LIBCXX_FOUND)
            file(GLOB MULTIARCH_LIBCXX "/usr/lib/x86_64-linux-gnu/libc++*.a")
            file(GLOB MULTIARCH_LIBCXXABI "/usr/lib/x86_64-linux-gnu/libc++abi*.a")
            list(SORT MULTIARCH_LIBCXX)
            list(SORT MULTIARCH_LIBCXXABI)
            list(REVERSE MULTIARCH_LIBCXX)  # Prefer higher versions
            list(REVERSE MULTIARCH_LIBCXXABI)

            if(MULTIARCH_LIBCXX AND MULTIARCH_LIBCXXABI)
                list(GET MULTIARCH_LIBCXX 0 LIBCXX_PATH)
                list(GET MULTIARCH_LIBCXXABI 0 LIBCXXABI_PATH)
                find_library(LIBUNWIND_PATH NAMES unwind)
                if(NOT LIBUNWIND_PATH)
                    message(WARNING "libunwind.a not found - exception handling may not work")
                endif()
                set(LLVM_RUNTIME_SOURCE "multiarch packages (${LIBCXX_PATH})")
                set(LIBCXX_FOUND TRUE)
            endif()
        endif()

        if(NOT LIBCXX_FOUND)
            message(FATAL_ERROR "LLVM runtime libraries not found. Install:\n"
                    "  Ubuntu/Debian: sudo apt-get install libc++-dev libc++abi-dev\n"
                    "  Arch Linux: sudo pacman -S libc++ libc++abi\n"
                    "  Fedora: sudo dnf install libcxx-devel libcxxabi-devel")
        endif()
    endif()  # if(EXISTS "/usr/local/lib/...)
    endif()  # if(NOT LIBCXX_FOUND) - Alpine fallback
    message(STATUS "Using LLVM runtime libraries (${LLVM_RUNTIME_SOURCE}):")
    message(STATUS "  libc++: ${LIBCXX_PATH}")
    message(STATUS "  libc++abi: ${LIBCXXABI_PATH}")
    if(LIBUNWIND_PATH)
        message(STATUS "  libunwind: ${LIBUNWIND_PATH}")
    endif()

    # Store Alpine libc++ paths for static executable linking
    # NOTE: Do NOT use link_libraries() here - it applies globally to ALL targets
    # including shared libraries. Alpine libc++ lacks -fPIC and cannot be used in
    # shared libraries. Instead, we store paths in cache variables and provide a
    # helper function to link them to static executables.
    set(ALPINE_LIBCXX_STATIC "${LIBCXX_PATH}" CACHE FILEPATH "Alpine libc++ for static executables" FORCE)
    set(ALPINE_LIBCXXABI_STATIC "${LIBCXXABI_PATH}" CACHE FILEPATH "Alpine libc++abi for static executables" FORCE)
    if(LIBUNWIND_PATH)
        set(ALPINE_LIBUNWIND_STATIC "${LIBUNWIND_PATH}" CACHE FILEPATH "Alpine libunwind for static executables" FORCE)
    endif()

    # Define helper function to link Alpine libc++ to static executables
    # Usage: link_alpine_libcxx(target_name)
    # Always uses PRIVATE keyword signature (modern CMake best practice)
    # Uses LINKER: syntax for proper linker directive handling
    function(link_alpine_libcxx TARGET_NAME)
        if(ALPINE_LIBCXX_STATIC AND ALPINE_LIBCXXABI_STATIC)
            # Link libc++ and libc++abi using explicit linker directives
            # The LINKER: prefix ensures CMake processes this as a linker flag
            target_link_libraries(${TARGET_NAME} PRIVATE
                "LINKER:--whole-archive"
                "${ALPINE_LIBCXX_STATIC}"
                "${ALPINE_LIBCXXABI_STATIC}"
                "LINKER:--no-whole-archive"
            )
            if(ALPINE_LIBUNWIND_STATIC)
                target_link_libraries(${TARGET_NAME} PRIVATE "${ALPINE_LIBUNWIND_STATIC}")
            endif()
            # Alpine's musl-compatible AArch64 libgcc provides the quad
            # conversion and outlined-atomic helpers required by the static
            # dependencies without introducing glibc-only runtime symbols.
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
                target_link_libraries(${TARGET_NAME} PRIVATE "${MUSL_LIBGCC_PATH}")
            endif()
            # Static musl executables must search libc after all dependency
            # archives so AArch64 sigsetjmp can branch to setjmp in range.
            target_link_libraries(${TARGET_NAME} PRIVATE c)
        endif()
    endfunction()

    # Search the target runtime after all application and dependency archives.
    set(CMAKE_C_STANDARD_LIBRARIES "\"${MUSL_LIBGCC_PATH}\" -lc -lm \"${MUSL_LIBDIR}/crtn.o\"" PARENT_SCOPE)
    set(CMAKE_CXX_STANDARD_LIBRARIES "\"${MUSL_LIBGCC_PATH}\" -lc -lm \"${MUSL_LIBDIR}/crtn.o\"" PARENT_SCOPE)

    # Disable fortify source for our code (musl doesn't support it)
    # Note: We now use Alpine's musl-native libc++ which doesn't have fortify calls
    target_compile_options(asciichat-project-options INTERFACE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)

    # Disable precompiled headers
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "Disable PCH for musl" FORCE)

    message(STATUS "${BoldBlue}musl${ColorReset} C library enabled (static linking with ${BoldCyan}clang${ColorReset})")
    message(STATUS "Using compiler runtime for musl: ${BoldMagenta}${MUSL_LIBGCC_PATH}${ColorReset}")
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

