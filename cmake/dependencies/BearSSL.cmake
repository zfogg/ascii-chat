# =============================================================================
# BearSSL Library Configuration
# =============================================================================
# Finds and configures BearSSL SSL/TLS library for HTTPS support
#
# BearSSL is used for:
#   - Fetching SSH keys from GitHub/GitLab via HTTPS
#   - SSL/TLS connection handling
#
# Build strategy:
#   1. Check for system-installed BearSSL (e.g., from Docker)
#   2. Build from submodule if system install not found
#   3. Cache built library for reuse across clean builds
#
# Prerequisites (must be set before including this file):
#   - WIN32, UNIX, APPLE: Platform detection variables
#   - USE_MUSL: Whether using musl libc
#   - CMAKE_BUILD_TYPE: Build type
#   - ASCIICHAT_DEPS_CACHE_DIR: Dependency cache directory
#
# Outputs (variables set by this file):
#   - BEARSSL_LIBRARIES: Libraries to link against (bearssl_static target)
#   - BEARSSL_INCLUDE_DIRS: Include directories
#   - BEARSSL_FOUND: Whether BearSSL was found or built successfully
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/Patching.cmake)
include(ProcessorCount)
ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 1)
endif()

# Handle musl builds - BearSSL is built from source
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}BearSSL${ColorReset} from source...")

    set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/bearssl")
    set(BEARSSL_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/bearssl-build")
    set(BEARSSL_LIB "${BEARSSL_BUILD_DIR}/libbearssl.a")

    if(EXISTS "${BEARSSL_SOURCE_DIR}")
        if(NOT EXISTS "${BEARSSL_LIB}")
            message(STATUS "  BearSSL library not found in cache, building from source...")
            file(MAKE_DIRECTORY "${BEARSSL_BUILD_DIR}")

            # Clean any previous build to avoid leftover targets
            execute_process(
                COMMAND make clean
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                OUTPUT_QUIET
                ERROR_QUIET
            )

            # Build the static library target with parallel jobs for faster builds
            # For musl: disable getentropy() (not in musl), force /dev/urandom, disable fortification
            execute_process(
                COMMAND make -j lib CC=${MUSL_GCC} AR=${CMAKE_AR} CFLAGS=-DBR_USE_GETENTROPY=0\ -DBR_USE_URANDOM=1\ -U_FORTIFY_SOURCE\ -D_FORTIFY_SOURCE=0\ -fno-stack-protector\ -fPIC
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                RESULT_VARIABLE BEARSSL_MAKE_RESULT
                OUTPUT_VARIABLE BEARSSL_MAKE_OUTPUT
                ERROR_VARIABLE BEARSSL_MAKE_ERROR
                OUTPUT_QUIET
            )

            if(BEARSSL_MAKE_RESULT EQUAL 0)
                # Copy library to cache
                file(COPY "${BEARSSL_SOURCE_DIR}/build/libbearssl.a"
                     DESTINATION "${BEARSSL_BUILD_DIR}")
                message(STATUS "  ${BoldBlue}BearSSL${ColorReset} library built and cached successfully")
            else()
                message(FATAL_ERROR "BearSSL build failed with exit code ${BEARSSL_MAKE_RESULT}\nOutput: ${BEARSSL_MAKE_OUTPUT}\nError: ${BEARSSL_MAKE_ERROR}")
            endif()
        else()
            message(STATUS "  ${BoldBlue}BearSSL${ColorReset} library found in cache: ${BoldMagenta}${BEARSSL_LIB}${ColorReset}")
        endif()

        # Create an imported library target that matches what BearSSL.cmake creates
        add_library(bearssl_static STATIC IMPORTED GLOBAL)
        set_target_properties(bearssl_static PROPERTIES
            IMPORTED_LOCATION "${BEARSSL_LIB}"
        )
        target_include_directories(bearssl_static INTERFACE
            "${BEARSSL_SOURCE_DIR}/inc"
        )

        set(BEARSSL_LIBRARIES bearssl_static)
        set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SOURCE_DIR}/inc")
        set(BEARSSL_FOUND TRUE)
    else()
        message(FATAL_ERROR "BearSSL submodule not found - GitHub/GitLab key fetching will be disabled")
        set(BEARSSL_LIBRARIES "")
        set(BEARSSL_INCLUDE_DIRS "")
        set(BEARSSL_FOUND FALSE)
    endif()

    set(BEARSSL_LIBRARIES "${BEARSSL_LIBRARIES}" PARENT_SCOPE)
    set(BEARSSL_INCLUDE_DIRS "${BEARSSL_INCLUDE_DIRS}" PARENT_SCOPE)
    set(BEARSSL_FOUND "${BEARSSL_FOUND}" PARENT_SCOPE)
    return()
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
    # Use system-installed BearSSL (from Docker or system package manager)
    add_library(bearssl_static STATIC IMPORTED)
    set_target_properties(bearssl_static PROPERTIES
        IMPORTED_LOCATION "${BEARSSL_SYSTEM_LIB}"
    )
    target_include_directories(bearssl_static INTERFACE "${BEARSSL_SYSTEM_INC}")
    set(BEARSSL_LIBRARIES bearssl_static)
    set(BEARSSL_INCLUDE_DIRS "${BEARSSL_SYSTEM_INC}")
    set(BEARSSL_FOUND TRUE)

    # Try to get version from system package manager
    execute_process(
        COMMAND pkg-config --modversion bearssl
        OUTPUT_VARIABLE BEARSSL_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(BEARSSL_VERSION)
        message(STATUS "Using system ${BoldGreen}BearSSL${ColorReset}, version ${BoldGreen}${BEARSSL_VERSION}${ColorReset}: ${BEARSSL_SYSTEM_LIB}")
    else()
        message(STATUS "Using system ${BoldGreen}BearSSL${ColorReset} library: ${BEARSSL_SYSTEM_LIB}")
    endif()

# Fall back to building from submodule
elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/bearssl")
    set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/bearssl")

    if(WIN32)
        # Windows: Build to cache directory (ASCIICHAT_DEPS_CACHE_DIR already includes build type)
        set(BEARSSL_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/bearssl")
        set(BEARSSL_LIB "${BEARSSL_BUILD_DIR}/bearssls.lib")

        file(MAKE_DIRECTORY "${BEARSSL_BUILD_DIR}")

        # Only build BearSSL if the cached library doesn't exist
        if(NOT EXISTS "${BEARSSL_LIB}")
            message(STATUS "${BoldYellow}BearSSL${ColorReset} library not found in cache, building from source...")

            # Apply Windows+Clang patch to BearSSL (fixes header conflicts with clang-cl)
            apply_patch(
                TARGET_DIR "${BEARSSL_SOURCE_DIR}"
                PATCH_FILE "${CMAKE_SOURCE_DIR}/cmake/dependencies/patches/bearssl-0-windows-clang.patch"
                PATCH_NUM 0
                DESCRIPTION "Fix Windows+Clang header conflicts"
                ASSUME_UNCHANGED src/inner.h
            )

            # Use centralized build tools from FindPrograms.cmake
            if(NOT ASCIICHAT_NMAKE_EXECUTABLE)
                message(FATAL_ERROR "nmake not found. Required for building BearSSL on Windows.")
            endif()
            set(NMAKE_EXECUTABLE "${ASCIICHAT_NMAKE_EXECUTABLE}")
            if(NOT ASCIICHAT_CLANG_CL_EXECUTABLE)
                message(FATAL_ERROR "clang-cl not found. Required for building BearSSL on Windows.")
            endif()
            set(CLANG_CL_EXECUTABLE "${ASCIICHAT_CLANG_CL_EXECUTABLE}")

            # Prefer llvm-lib over MSVC lib.exe for consistency across architectures
            if(ASCIICHAT_LLVM_LIB_EXECUTABLE)
                set(BEARSSL_AR_EXECUTABLE "${ASCIICHAT_LLVM_LIB_EXECUTABLE}")
                message(STATUS "BearSSL: Using llvm-lib: ${ASCIICHAT_LLVM_LIB_EXECUTABLE}")
            elseif(ASCIICHAT_MSVC_LIB_EXECUTABLE)
                set(BEARSSL_AR_EXECUTABLE "${ASCIICHAT_MSVC_LIB_EXECUTABLE}")
                message(STATUS "BearSSL: Using MSVC lib.exe: ${ASCIICHAT_MSVC_LIB_EXECUTABLE}")
            else()
                message(FATAL_ERROR "No archiver found (llvm-lib or lib.exe). Required for building BearSSL on Windows.")
            endif()

            # Build BearSSL at configure time using PowerShell to avoid MAKEFLAGS issues
            # PowerShell provides a clean environment without inheriting bash/make variables
            set(BEARSSL_LOG_FILE "${BEARSSL_BUILD_DIR}/bearssl-build.log")

            # Ensure obj directory exists
            file(MAKE_DIRECTORY "${BEARSSL_SOURCE_DIR}/build/obj")

            # Convert paths to Windows format for nmake
            file(TO_NATIVE_PATH "${BEARSSL_SOURCE_DIR}" BEARSSL_SOURCE_DIR_NATIVE)
            file(TO_NATIVE_PATH "${NMAKE_EXECUTABLE}" NMAKE_NATIVE)
            file(TO_NATIVE_PATH "${CLANG_CL_EXECUTABLE}" CLANG_CL_NATIVE)
            file(TO_NATIVE_PATH "${BEARSSL_AR_EXECUTABLE}" AR_NATIVE)

            execute_process(
                COMMAND powershell -NoProfile -Command "
                    $env:MAKEFLAGS = $null;
                    $env:NMAKEFLAGS = $null;
                    Set-Location '${BEARSSL_SOURCE_DIR_NATIVE}';
                    & '${NMAKE_NATIVE}' /F mk\\NMake.mk CC='${CLANG_CL_NATIVE}' AR='${AR_NATIVE}' lib
                "
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                RESULT_VARIABLE BEARSSL_BUILD_RESULT
                OUTPUT_FILE "${BEARSSL_LOG_FILE}"
                ERROR_FILE "${BEARSSL_LOG_FILE}"
            )

            if(NOT BEARSSL_BUILD_RESULT EQUAL 0)
                message(FATAL_ERROR "${BoldRed}BearSSL build failed${ColorReset}. Check log: ${BEARSSL_LOG_FILE}")
            endif()

            # Copy built library to cache
            file(COPY_FILE "${BEARSSL_SOURCE_DIR}/build/bearssls.lib" "${BEARSSL_LIB}")

            message(STATUS "  ${BoldGreen}BearSSL${ColorReset} library built and cached successfully")

            # Create a dummy target so dependencies work
            add_custom_target(bearssl_build)
        else()
            message(STATUS "${BoldGreen}BearSSL${ColorReset} library found in cache: ${BoldCyan}${BEARSSL_LIB}${ColorReset}")
            # Create a dummy target so dependencies work
            add_custom_target(bearssl_build)
        endif()

    else()
        # Unix/Linux/macOS
        # Build to cache directory (ASCIICHAT_DEPS_CACHE_DIR already includes build type)
        set(BEARSSL_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/bearssl")
        set(BEARSSL_LIB "${BEARSSL_BUILD_DIR}/libbearssl.a")

        file(MAKE_DIRECTORY "${BEARSSL_BUILD_DIR}")

        # Only build if library doesn't exist in cache
        if(NOT EXISTS "${BEARSSL_LIB}")
            message(STATUS "${BoldYellow}BearSSL${ColorReset} library not found in cache, building from source...")

            # Always add -fPIC for shared library support
            set(BEARSSL_EXTRA_CFLAGS "-fPIC")
            set(BEARSSL_CC "${CMAKE_C_COMPILER}")

            # Clean BearSSL build directory before initial build
            execute_process(
                COMMAND make clean
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                OUTPUT_QUIET
                ERROR_QUIET
            )

            # Build BearSSL at configure time using execute_process
            # This is more reliable than add_custom_command which has issues with
            # make on macOS GitHub Actions runners (commands echo but don't execute)
            set(BEARSSL_LOG_FILE "${BEARSSL_BUILD_DIR}/bearssl-build.log")
            execute_process(
                COMMAND make -j${NPROC} lib "CC=${BEARSSL_CC}" "AR=${CMAKE_AR}" "CFLAGS=${BEARSSL_EXTRA_CFLAGS}"
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                RESULT_VARIABLE BEARSSL_BUILD_RESULT
                OUTPUT_FILE "${BEARSSL_LOG_FILE}"
                ERROR_FILE "${BEARSSL_LOG_FILE}"
            )

            if(NOT BEARSSL_BUILD_RESULT EQUAL 0)
                message(FATAL_ERROR "${BoldRed}BearSSL build failed${ColorReset}. Check log: ${BEARSSL_LOG_FILE}")
            endif()

            # Copy built library to cache
            file(COPY_FILE "${BEARSSL_SOURCE_DIR}/build/libbearssl.a" "${BEARSSL_LIB}")

            message(STATUS "  ${BoldGreen}BearSSL${ColorReset} library built and cached successfully")
        else()
            message(STATUS "${BoldGreen}BearSSL${ColorReset} library found in cache: ${BoldCyan}${BEARSSL_LIB}${ColorReset}")
        endif()

        # Create a dummy target so dependencies work (library already built at configure time)
        add_custom_target(bearssl_build)
    endif()

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

    # Get version from git submodule
    execute_process(
        COMMAND git -C "${BEARSSL_SOURCE_DIR}" describe --tags --always
        OUTPUT_VARIABLE BEARSSL_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(BEARSSL_VERSION)
        message(STATUS "${BoldGreen}BearSSL${ColorReset} configured, version ${BoldGreen}${BEARSSL_VERSION}${ColorReset}: ${BEARSSL_LIB}")
    else()
        message(STATUS "${BoldGreen}BearSSL${ColorReset} configured: ${BEARSSL_LIB}")
    endif()
else()
    message(FATAL_ERROR "${BoldRed}BearSSL submodule not found${ColorReset}")
    set(BEARSSL_FOUND FALSE)
    set(BEARSSL_LIBRARIES "")
    set(BEARSSL_INCLUDE_DIRS "")
endif()
