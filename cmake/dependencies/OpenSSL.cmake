#
# OpenSSL dependency configuration
# Provides: OpenSSL::Crypto, OpenSSL::SSL
#

# Skip if already configured by MuslDependencies.cmake
if(TARGET OpenSSL::Crypto)
    message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} already configured")
    return()
endif()

find_package(OpenSSL)

if(OpenSSL_FOUND)
    message(STATUS "  ${BoldBlue}OpenSSL${ColorReset} ${BoldGreen}found${ColorReset}: ${OPENSSL_VERSION}")
else()
    message(FATAL_ERROR "OpenSSL not found. Please install OpenSSL development libraries.")
endif()
