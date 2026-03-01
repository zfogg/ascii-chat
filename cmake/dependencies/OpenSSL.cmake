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
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        set(OPENSSL_TARGET "linux-x86_64")
    else()
        set(OPENSSL_TARGET "linux-generic64")
    endif()

    # Build OpenSSL synchronously at configure time if not cached
    # Note: OpenSSL 3.x installs to lib64/ by default on x86_64
    if(NOT EXISTS "${OPENSSL_PREFIX}/lib64/libssl.a" OR NOT EXISTS "${OPENSSL_PREFIX}/lib64/libcrypto.a")
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
        message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} library found in cache: ${BoldMagenta}${OPENSSL_PREFIX}/lib64/libssl.a${ColorReset}")
    endif()

    # Set OpenSSL variables for CMake find_package to use
    # Note: OpenSSL 3.x installs to lib64/ by default on x86_64
    set(OPENSSL_ROOT_DIR "${OPENSSL_PREFIX}" CACHE PATH "OpenSSL root directory" FORCE)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_PREFIX}/include" CACHE PATH "OpenSSL include directory" FORCE)
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_PREFIX}/lib64/libssl.a" CACHE FILEPATH "OpenSSL SSL library" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_PREFIX}/lib64/libcrypto.a" CACHE FILEPATH "OpenSSL Crypto library" FORCE)

    # Create imported targets that match what find_package(OpenSSL) would create
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_PREFIX}/lib64/libcrypto.a"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
        )
    endif()

    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_PREFIX}/lib64/libssl.a"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
        )
    endif()

    return()
endif()

# Skip if already configured by MuslDependencies.cmake
if(TARGET OpenSSL::Crypto)
    message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} already configured")
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
    find_package(OpenSSL)

    if(OpenSSL_FOUND)
        message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} ${BoldGreen}found${ColorReset}: ${OPENSSL_VERSION}")
    else()
        message(FATAL_ERROR "OpenSSL not found. Please install OpenSSL development libraries.")
    endif()
endif()
