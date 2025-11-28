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
elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/bearssl")
    set(BEARSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/bearssl")

    if(WIN32)
        # Windows: Build to cache directory (ASCIICHAT_DEPS_CACHE_DIR already includes build type)
        set(BEARSSL_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/bearssl")
        set(BEARSSL_LIB "${BEARSSL_BUILD_DIR}/bearssls.lib")

        file(MAKE_DIRECTORY "${BEARSSL_BUILD_DIR}")

        # Only build BearSSL if the cached library doesn't exist
        if(NOT EXISTS "${BEARSSL_LIB}")
            message(STATUS "${BoldYellow}BearSSL${ColorReset} library not found in cache, will build from source: ${BoldCyan}${BEARSSL_LIB}${ColorReset}")

            # Apply Windows+Clang patch to BearSSL (fixes header conflicts with clang-cl)
            # This needs to happen at configure time to ensure the source is patched
            set(BEARSSL_PATCH_FILE "${CMAKE_SOURCE_DIR}/cmake/dependencies/patches/bearssl-windows-clang.patch")
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
                COMMAND ${CMAKE_COMMAND} -E env
                        MAKEFLAGS=
                        NMAKEFLAGS=
                        "${NMAKE_EXECUTABLE}" "CC=${CLANG_CL_EXECUTABLE}" "AR=${LLVM_LIB_EXECUTABLE}" lib
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEARSSL_SOURCE_DIR}/build/bearssls.lib" "${BEARSSL_LIB}"
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                COMMENT "Building BearSSL library to cache: ${BEARSSL_BUILD_DIR}"
                VERBATIM
            )

            # Add custom target that depends on the library
            add_custom_target(bearssl_build DEPENDS "${BEARSSL_LIB}")
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

        # Only add build command if library doesn't exist
        if(NOT EXISTS "${BEARSSL_LIB}")
            message(STATUS "${BoldYellow}BearSSL${ColorReset} library not found in cache, will build from source: ${BoldCyan}${BEARSSL_LIB}${ColorReset}")

            # For musl builds: disable getentropy() (not in musl), force /dev/urandom, disable fortification
            # Always add -fPIC for shared library support
            if(USE_MUSL)
                set(BEARSSL_EXTRA_CFLAGS "-fPIC -DBR_USE_GETENTROPY=0 -DBR_USE_URANDOM=1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector")
                set(BEARSSL_CC "/usr/bin/musl-gcc")
            else()
                set(BEARSSL_EXTRA_CFLAGS "-fPIC")
                set(BEARSSL_CC "${CMAKE_C_COMPILER}")
            endif()

            # Clean BearSSL build directory before initial build
            message(STATUS "  Cleaning BearSSL build directory before initial build...")
            execute_process(
                COMMAND make clean
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                OUTPUT_QUIET
                ERROR_QUIET
            )

            add_custom_command(
                OUTPUT "${BEARSSL_LIB}"
                COMMAND ${CMAKE_COMMAND} -E echo "Building BearSSL library (static only) with make lib..."
                COMMAND make lib CC=${BEARSSL_CC} AR=${CMAKE_AR} "CFLAGS=${BEARSSL_EXTRA_CFLAGS}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEARSSL_SOURCE_DIR}/build/libbearssl.a" "${BEARSSL_LIB}"
                WORKING_DIRECTORY "${BEARSSL_SOURCE_DIR}"
                COMMENT "Building BearSSL static library to cache: ${BEARSSL_BUILD_DIR}"
                VERBATIM
            )

            # Add custom target that depends on the library
            add_custom_target(bearssl_build DEPENDS "${BEARSSL_LIB}")
        else()
            message(STATUS "${BoldGreen}BearSSL${ColorReset} library found in cache: ${BoldCyan}${BEARSSL_LIB}${ColorReset}")
            # Create a dummy target so dependencies work
            add_custom_target(bearssl_build)
        endif()
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
