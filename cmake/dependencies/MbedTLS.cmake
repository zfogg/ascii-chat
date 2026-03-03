#
# mbedTLS dependency configuration
# Provides: mbedtls::mbedtls, mbedtls::mbedcrypto, mbedtls::mbedx509
#
# Used for WebSocket Secure (WSS) support when building libwebsockets with mbedTLS backend

include(${CMAKE_SOURCE_DIR}/cmake/utils/FindDependency.cmake)

# Try to find mbedTLS using pkg-config or vcpkg
find_dependency_library(
    NAME MBEDTLS
    VCPKG_NAMES mbedtls
    HEADER mbedtls/ssl.h
    PKG_CONFIG mbedtls
    HOMEBREW_PKG mbedtls
    STATIC_LIB_NAMES mbedtls mbedcrypto mbedx509
    OPTIONAL
)

# If not found, mbedTLS is optional - it's only needed if using libwebsockets with mbedTLS backend
if(NOT MBEDTLS_FOUND)
    message(STATUS "  ${BoldYellow}mbedTLS${ColorReset} not found (optional - only needed for WSS with mbedTLS backend)")
endif()
