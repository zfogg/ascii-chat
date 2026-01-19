#
# OpenSSL dependency configuration
# Provides: OpenSSL::Crypto, OpenSSL::SSL (or static equivalents)
#

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
    if(EXISTS "/opt/homebrew/opt/openssl@3/lib")
        set(_OPENSSL_HOMEBREW_PATH "/opt/homebrew/opt/openssl@3")
    elseif(EXISTS "/usr/local/opt/openssl@3/lib")
        set(_OPENSSL_HOMEBREW_PATH "/usr/local/opt/openssl@3")
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
