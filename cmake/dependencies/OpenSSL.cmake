#
# OpenSSL dependency configuration
# Provides: OpenSSL::Crypto, OpenSSL::SSL (or static equivalents)
#

# Handle musl builds - OpenSSL is built from source at configure time
if(USE_MUSL)
    # OpenSSL MUST be built synchronously at configure time because libdatachannel's
    # configure also runs at configure time (via execute_process in Libdatachannel.cmake).
    # If we use ExternalProject (which runs at build time), OpenSSL won't exist when
    # libdatachannel tries to find_package(OpenSSL).
    message(STATUS "Configuring ${BoldBlue}OpenSSL${ColorReset} from source...")

    set(OPENSSL_PREFIX "${MUSL_DEPS_DIR_STATIC}/openssl")
    set(OPENSSL_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/openssl-build")
    set(OPENSSL_SOURCE_DIR "${OPENSSL_BUILD_DIR}/src/openssl")

    # Detect target architecture for OpenSSL Configure
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(OPENSSL_TARGET "linux-aarch64")
        set(OPENSSL_LIBDIR "lib64")  # 64-bit
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        set(OPENSSL_TARGET "linux-x86_64")
        set(OPENSSL_LIBDIR "lib64")  # 64-bit
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i386|i686")
        set(OPENSSL_TARGET "linux-x86")
        set(OPENSSL_LIBDIR "lib")    # 32-bit
    else()
        set(OPENSSL_TARGET "linux-generic64")
        set(OPENSSL_LIBDIR "lib64")  # Default to 64-bit
    endif()

    # Build OpenSSL synchronously at configure time if not cached
    if(NOT EXISTS "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a" OR NOT EXISTS "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a")
        message(STATUS "  OpenSSL library not found in cache, will build from source")
        message(STATUS "  This may take a few minutes on first build...")

        file(MAKE_DIRECTORY "${OPENSSL_BUILD_DIR}")
        file(MAKE_DIRECTORY "${OPENSSL_SOURCE_DIR}")

        # Download OpenSSL source
        set(OPENSSL_TARBALL "${OPENSSL_BUILD_DIR}/openssl-3.4.0.tar.gz")
        if(NOT EXISTS "${OPENSSL_TARBALL}")
            message(STATUS "  Downloading OpenSSL 3.4.0...")
            file(DOWNLOAD
                "https://github.com/openssl/openssl/releases/download/openssl-3.4.0/openssl-3.4.0.tar.gz"
                "${OPENSSL_TARBALL}"
                EXPECTED_HASH SHA256=e15dda82fe2fe8139dc2ac21a36d4ca01d5313c75f99f46c4e8a27709b7294bf
                STATUS DOWNLOAD_STATUS
                SHOW_PROGRESS
            )
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                message(FATAL_ERROR "Failed to download OpenSSL: ${ERROR_MSG}")
            endif()
        endif()

        # Extract tarball
        if(NOT EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
            message(STATUS "  Extracting OpenSSL...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${OPENSSL_TARBALL}"
                WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract OpenSSL tarball")
            endif()
            # Move from openssl-3.4.0/ to src/openssl/
            file(RENAME "${OPENSSL_BUILD_DIR}/openssl-3.4.0" "${OPENSSL_SOURCE_DIR}")
        endif()

        # Configure OpenSSL
        message(STATUS "  Configuring OpenSSL for ${OPENSSL_TARGET}...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env
                CC=${MUSL_GCC}
                REALGCC=${REAL_GCC}
                CFLAGS=${MUSL_KERNEL_CFLAGS}
                "${OPENSSL_SOURCE_DIR}/Configure"
                ${OPENSSL_TARGET}
                --prefix=${OPENSSL_PREFIX}
                no-shared
                no-tests
                no-ui-console
                -fPIC
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE CONFIG_RESULT
            OUTPUT_VARIABLE CONFIG_OUTPUT
            ERROR_VARIABLE CONFIG_ERROR
        )
        if(NOT CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure OpenSSL:\n${CONFIG_ERROR}")
        endif()

        message(STATUS "  Building OpenSSL (this takes a few minutes)...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env REALGCC=${REAL_GCC} make -j
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build OpenSSL:\n${BUILD_ERROR}")
        endif()

        # Install OpenSSL
        message(STATUS "  Installing OpenSSL...")
        execute_process(
            COMMAND make install_sw
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE INSTALL_RESULT
            OUTPUT_VARIABLE INSTALL_OUTPUT
            ERROR_VARIABLE INSTALL_ERROR
        )
        if(NOT INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to install OpenSSL:\n${INSTALL_ERROR}")
        endif()

        message(STATUS "  ${BoldGreen}OpenSSL${ColorReset} built and cached successfully")
    else()
        message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} library found in cache: ${BoldMagenta}${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a${ColorReset}")
    endif()

    # Set OpenSSL variables for CMake find_package to use
    set(OPENSSL_ROOT_DIR "${OPENSSL_PREFIX}" CACHE PATH "OpenSSL root directory" FORCE)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_PREFIX}/include" CACHE PATH "OpenSSL include directory" FORCE)
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a" CACHE FILEPATH "OpenSSL SSL library" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a" CACHE FILEPATH "OpenSSL Crypto library" FORCE)

    # Create imported targets that match what find_package(OpenSSL) would create
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
        )
    endif()

    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
        )
    endif()

    # Create a custom target for musl builds (libraries are pre-built/cached)
    add_custom_target(openssl-musl)

    return()
endif()

# Skip if already configured by MuslDependencies.cmake
if(TARGET OpenSSL::Crypto)
    message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} already configured")
    return()
endif()

# =============================================================================
# Native Linux/macOS: Build OpenSSL 3.4.0 from source for libwebsockets
# =============================================================================
if(NOT USE_MUSL AND NOT WIN32 AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev"))
    message(STATUS "Configuring ${BoldBlue}OpenSSL 3.4.0${ColorReset} from source for libwebsockets...")

    set(OPENSSL_PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/openssl")
    set(OPENSSL_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/openssl-build")
    set(OPENSSL_SOURCE_DIR "${OPENSSL_BUILD_DIR}/src/openssl")
    set(OPENSSL_TARGET_STAMP "${OPENSSL_BUILD_DIR}/.target")
    set(OPENSSL_NO_ASM OFF)
    set(OPENSSL_BUILD_SHARED ON)

    # Detect target architecture for OpenSSL Configure.
    # Important: Check APPLE first so arm64 Macs don't get classified as linux-aarch64.
    if(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(OPENSSL_TARGET "darwin64-arm64-cc")
        else()
            set(OPENSSL_TARGET "darwin64-x86_64-cc")
        endif()
        set(OPENSSL_LIBDIR "lib")
        set(OPENSSL_NO_ASM ON)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(OPENSSL_TARGET "linux-aarch64")
        set(OPENSSL_LIBDIR "lib64")  # 64-bit
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        set(OPENSSL_TARGET "linux-x86_64")
        set(OPENSSL_LIBDIR "lib64")  # 64-bit
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i386|i686")
        set(OPENSSL_TARGET "linux-x86")
        set(OPENSSL_LIBDIR "lib")    # 32-bit
    else()
        set(OPENSSL_TARGET "linux-generic64")
        set(OPENSSL_LIBDIR "lib64")  # Default to 64-bit
    endif()
    set(OPENSSL_CONFIG_SIGNATURE "target=${OPENSSL_TARGET};no_asm=${OPENSSL_NO_ASM};shared=${OPENSSL_BUILD_SHARED}")

    if(OPENSSL_BUILD_SHARED)
        set(_OPENSSL_EXPECTED_SSL_LIB "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(_OPENSSL_EXPECTED_CRYPTO_LIB "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(_OPENSSL_IMPORTED_TYPE SHARED)
    else()
        set(_OPENSSL_EXPECTED_SSL_LIB "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl.a")
        set(_OPENSSL_EXPECTED_CRYPTO_LIB "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto.a")
        set(_OPENSSL_IMPORTED_TYPE STATIC)
    endif()

    # If cached OpenSSL was generated for a different target, wipe and re-extract.
    set(_OPENSSL_RECONFIGURE_REQUIRED FALSE)
    if(EXISTS "${OPENSSL_TARGET_STAMP}")
        file(READ "${OPENSSL_TARGET_STAMP}" _OPENSSL_CACHED_SIGNATURE)
        string(STRIP "${_OPENSSL_CACHED_SIGNATURE}" _OPENSSL_CACHED_SIGNATURE)
        if(NOT _OPENSSL_CACHED_SIGNATURE STREQUAL "${OPENSSL_CONFIG_SIGNATURE}")
            set(_OPENSSL_RECONFIGURE_REQUIRED TRUE)
            message(STATUS "  OpenSSL configuration changed: ${_OPENSSL_CACHED_SIGNATURE} -> ${OPENSSL_CONFIG_SIGNATURE}; forcing clean reconfigure")
        endif()
    elseif(EXISTS "${OPENSSL_SOURCE_DIR}/configdata.pm")
        # Legacy cache from before signature stamping; force a clean rebuild.
        set(_OPENSSL_RECONFIGURE_REQUIRED TRUE)
        message(STATUS "  OpenSSL cache missing configuration signature; forcing clean reconfigure")
    endif()

    if(_OPENSSL_RECONFIGURE_REQUIRED)
        file(REMOVE_RECURSE "${OPENSSL_SOURCE_DIR}")
        file(REMOVE_RECURSE "${OPENSSL_PREFIX}")
    endif()

    # Build OpenSSL 3.4.0 if not cached
    if(NOT EXISTS "${_OPENSSL_EXPECTED_SSL_LIB}" OR NOT EXISTS "${_OPENSSL_EXPECTED_CRYPTO_LIB}")
        message(STATUS "  OpenSSL 3.4.0 not found in cache, building from source...")

        file(MAKE_DIRECTORY "${OPENSSL_BUILD_DIR}")
        file(MAKE_DIRECTORY "${OPENSSL_SOURCE_DIR}")

        # Download OpenSSL source
        set(OPENSSL_TARBALL "${OPENSSL_BUILD_DIR}/openssl-3.4.0.tar.gz")
        if(NOT EXISTS "${OPENSSL_TARBALL}")
            message(STATUS "  Downloading OpenSSL 3.4.0...")
            file(DOWNLOAD
                "https://github.com/openssl/openssl/releases/download/openssl-3.4.0/openssl-3.4.0.tar.gz"
                "${OPENSSL_TARBALL}"
                EXPECTED_HASH SHA256=e15dda82fe2fe8139dc2ac21a36d4ca01d5313c75f99f46c4e8a27709b7294bf
                SHOW_PROGRESS
            )
        endif()

        # Extract tarball
        if(NOT EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
            message(STATUS "  Extracting OpenSSL...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${OPENSSL_TARBALL}"
                WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract OpenSSL tarball")
            endif()
            file(RENAME "${OPENSSL_BUILD_DIR}/openssl-3.4.0" "${OPENSSL_SOURCE_DIR}")
        endif()

        # Configure OpenSSL
        set(OPENSSL_CONFIGURE_ARGS
            ${OPENSSL_TARGET}
            --prefix=${OPENSSL_PREFIX}
            no-tests
            -fPIC
        )
        if(OPENSSL_NO_ASM)
            list(APPEND OPENSSL_CONFIGURE_ARGS no-asm)
        endif()
        if(OPENSSL_BUILD_SHARED)
            list(APPEND OPENSSL_CONFIGURE_ARGS shared)
        else()
            list(APPEND OPENSSL_CONFIGURE_ARGS no-shared)
        endif()
        message(STATUS "  Configuring OpenSSL for ${OPENSSL_TARGET}...")
        execute_process(
            COMMAND "${OPENSSL_SOURCE_DIR}/Configure"
                ${OPENSSL_CONFIGURE_ARGS}
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE CONFIG_RESULT
            OUTPUT_VARIABLE CONFIG_OUTPUT
            ERROR_VARIABLE CONFIG_ERROR
        )
        if(NOT CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure OpenSSL:\n${CONFIG_ERROR}")
        endif()

        message(STATUS "  Building OpenSSL (this takes a few minutes)...")
        execute_process(
            COMMAND make -j
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE BUILD_RESULT
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build OpenSSL")
        endif()

        # Install OpenSSL
        message(STATUS "  Installing OpenSSL...")
        execute_process(
            COMMAND make install_sw
            WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
            RESULT_VARIABLE INSTALL_RESULT
        )
        if(NOT INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to install OpenSSL")
        endif()

        file(WRITE "${OPENSSL_TARGET_STAMP}" "${OPENSSL_CONFIG_SIGNATURE}\n")

        message(STATUS "  ${BoldGreen}OpenSSL 3.4.0${ColorReset} built and cached successfully")
    else()
        message(STATUS "  ${BoldBlue}OpenSSL 3.4.0${ColorReset} found in cache: ${BoldMagenta}${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}${ColorReset}")
    endif()

    # Resolve installed OpenSSL library paths (handle versioned shared-library names).
    set(OPENSSL_SSL_LIBRARY "${_OPENSSL_EXPECTED_SSL_LIB}")
    set(OPENSSL_CRYPTO_LIBRARY "${_OPENSSL_EXPECTED_CRYPTO_LIB}")
    if(OPENSSL_BUILD_SHARED)
        if(NOT EXISTS "${OPENSSL_SSL_LIBRARY}")
            file(GLOB _OPENSSL_SSL_CANDIDATES "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libssl*${CMAKE_SHARED_LIBRARY_SUFFIX}")
            list(SORT _OPENSSL_SSL_CANDIDATES)
            list(LENGTH _OPENSSL_SSL_CANDIDATES _OPENSSL_SSL_CANDIDATE_COUNT)
            if(_OPENSSL_SSL_CANDIDATE_COUNT GREATER 0)
                list(GET _OPENSSL_SSL_CANDIDATES 0 OPENSSL_SSL_LIBRARY)
            endif()
        endif()
        if(NOT EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
            file(GLOB _OPENSSL_CRYPTO_CANDIDATES "${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}/libcrypto*${CMAKE_SHARED_LIBRARY_SUFFIX}")
            list(SORT _OPENSSL_CRYPTO_CANDIDATES)
            list(LENGTH _OPENSSL_CRYPTO_CANDIDATES _OPENSSL_CRYPTO_CANDIDATE_COUNT)
            if(_OPENSSL_CRYPTO_CANDIDATE_COUNT GREATER 0)
                list(GET _OPENSSL_CRYPTO_CANDIDATES 0 OPENSSL_CRYPTO_LIBRARY)
            endif()
        endif()
    endif()
    if(NOT EXISTS "${OPENSSL_SSL_LIBRARY}" OR NOT EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
        message(FATAL_ERROR "OpenSSL build succeeded but libraries were not found in ${OPENSSL_PREFIX}/${OPENSSL_LIBDIR}")
    endif()

    set(OPENSSL_ROOT_DIR "${OPENSSL_PREFIX}" CACHE PATH "OpenSSL root directory" FORCE)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_PREFIX}/include" CACHE PATH "OpenSSL include directory" FORCE)
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_SSL_LIBRARY}" CACHE FILEPATH "OpenSSL SSL library" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_CRYPTO_LIBRARY}" CACHE FILEPATH "OpenSSL Crypto library" FORCE)

    # Create imported targets for OpenSSL 3.4.0
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto ${_OPENSSL_IMPORTED_TYPE} IMPORTED GLOBAL)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
        )
    endif()

    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL ${_OPENSSL_IMPORTED_TYPE} IMPORTED GLOBAL)
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
        )
    endif()

    set(OpenSSL_FOUND TRUE)
    set(OPENSSL_VERSION "3.4.0")
    message(STATUS "  ${BoldGreen}✓${ColorReset} OpenSSL 3.4.0 configured for native build")
    return()
endif()

# =============================================================================
# macOS Release: Try static libraries first when ASCIICHAT_SHARED_DEPS is OFF
# =============================================================================
set(_OPENSSL_STATIC_FOUND FALSE)
if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_SHARED_DEPS)
    # Find static OpenSSL libraries from Homebrew
    set(_OPENSSL_HOMEBREW_PATH "")
    if(HOMEBREW_PREFIX AND EXISTS "${HOMEBREW_PREFIX}/opt/openssl@3/lib")
        set(_OPENSSL_HOMEBREW_PATH "${HOMEBREW_PREFIX}/opt/openssl@3")
    endif()

    if(_OPENSSL_HOMEBREW_PATH)
        find_library(OPENSSL_CRYPTO_STATIC NAMES libcrypto.a
            PATHS "${_OPENSSL_HOMEBREW_PATH}/lib" NO_DEFAULT_PATH)
        find_library(OPENSSL_SSL_STATIC NAMES libssl.a
            PATHS "${_OPENSSL_HOMEBREW_PATH}/lib" NO_DEFAULT_PATH)
        find_path(OPENSSL_INCLUDE_DIR NAMES openssl/ssl.h
            PATHS "${_OPENSSL_HOMEBREW_PATH}/include" NO_DEFAULT_PATH)

        if(OPENSSL_CRYPTO_STATIC AND OPENSSL_SSL_STATIC AND OPENSSL_INCLUDE_DIR)
            set(_OPENSSL_STATIC_FOUND TRUE)

            # Create imported targets for static OpenSSL
            add_library(OpenSSL::Crypto STATIC IMPORTED)
            set_target_properties(OpenSSL::Crypto PROPERTIES
                IMPORTED_LOCATION "${OPENSSL_CRYPTO_STATIC}"
                INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
            )

            add_library(OpenSSL::SSL STATIC IMPORTED)
            set_target_properties(OpenSSL::SSL PROPERTIES
                IMPORTED_LOCATION "${OPENSSL_SSL_STATIC}"
                INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
            )

            # Get version from the static library's path
            execute_process(
                COMMAND "${_OPENSSL_HOMEBREW_PATH}/bin/openssl" version
                OUTPUT_VARIABLE _OPENSSL_VERSION_OUTPUT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" OPENSSL_VERSION "${_OPENSSL_VERSION_OUTPUT}")

            set(OpenSSL_FOUND TRUE)
            message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} ${BoldGreen}found${ColorReset} (macOS static): ${OPENSSL_VERSION}")
            message(STATUS "    - libcrypto: ${OPENSSL_CRYPTO_STATIC}")
            message(STATUS "    - libssl: ${OPENSSL_SSL_STATIC}")
        endif()
    endif()
endif()

# =============================================================================
# Fallback: Use find_package (dynamic linking)
# =============================================================================
if(NOT _OPENSSL_STATIC_FOUND)
    if(WIN32 AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev"))
        # Force shared OpenSSL on Windows debug/dev builds.
        set(OPENSSL_USE_STATIC_LIBS FALSE)
    endif()
    find_package(OpenSSL)

    if(OpenSSL_FOUND)
        message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} ${BoldGreen}found${ColorReset}: ${OPENSSL_VERSION}")
    else()
        message(FATAL_ERROR "OpenSSL not found. Please install OpenSSL development libraries.")
    endif()
endif()
