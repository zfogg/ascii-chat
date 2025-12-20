# =============================================================================
# Cryptography Module (ascii-chat-crypto)
# =============================================================================
# End-to-end encryption with libsodium (X25519, XSalsa20-Poly1305, Ed25519)
# SSH key authentication and known hosts verification
# =============================================================================

set(CRYPTO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/keys/keys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/known_hosts.c
    ${CMAKE_CURRENT_SOURCE_DIR}/handshake.c
    ${CMAKE_CURRENT_SOURCE_DIR}/http_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pem_utils.c
    # lib/crypto/gpg.c - Temporarily excluded
    ${CMAKE_CURRENT_SOURCE_DIR}/ssh_agent.c
    ${CMAKE_CURRENT_SOURCE_DIR}/keys/ssh_keys.c
    # lib/crypto/keys/gpg_keys.c - Temporarily excluded
    ${CMAKE_CURRENT_SOURCE_DIR}/keys/gpg_stubs.c  # Stub implementations for disabled GPG
    ${CMAKE_CURRENT_SOURCE_DIR}/keys/https_keys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/keys/validation.c
    # libsodium-bcrypt-pbkdf (OpenBSD implementation)
    ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
)

create_ascii_chat_module(ascii-chat-crypto "${CRYPTO_SRCS}")

# =============================================================================
# Third-party Code Configuration
# =============================================================================

# Suppress specific Clang warnings for libsodium-bcrypt-pbkdf (third-party code)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(BCRYPT_PBKDF_WARNINGS "-Wno-sizeof-array-div")
    if(HAVE_WNO_UNTERMINATED_STRING_INIT)
        list(APPEND BCRYPT_PBKDF_WARNINGS "-Wno-unterminated-string-initialization")
    endif()
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
        PROPERTIES COMPILE_OPTIONS "${BCRYPT_PBKDF_WARNINGS}"
    )
endif()

# Disable static analyzers for third-party libsodium-bcrypt-pbkdf code
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
    PROPERTIES
    SKIP_LINTING ON
)

# =============================================================================
# Include Directories
# =============================================================================

# libsodium-bcrypt-pbkdf includes
target_include_directories(ascii-chat-crypto PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/include
    ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src
)

# libsodium include directory (for GCC builds from source)
if(LIBSODIUM_INCLUDE_DIRS)
    target_include_directories(ascii-chat-crypto PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
endif()

# BearSSL includes
if(BEARSSL_FOUND)
    target_include_directories(ascii-chat-crypto PRIVATE ${BEARSSL_INCLUDE_DIRS})
endif()

# =============================================================================
# Dependencies
# =============================================================================

if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-crypto
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-network
        ${LIBSODIUM_LIBRARIES}
    )
else()
    # For OBJECT libs, link external deps only (no module deps)
    target_link_libraries(ascii-chat-crypto ${LIBSODIUM_LIBRARIES})
endif()

# Add dependency on libsodium build target if building from source
if(DEFINED LIBSODIUM_BUILD_TARGET)
    add_dependencies(ascii-chat-crypto ${LIBSODIUM_BUILD_TARGET})
endif()

# BearSSL for HTTPS key fetching
if(BEARSSL_FOUND)
    target_link_libraries(ascii-chat-crypto ${BEARSSL_LIBRARIES})
endif()

# Export sources to parent scope for unified library builds
set(CRYPTO_SRCS ${CRYPTO_SRCS} PARENT_SCOPE)
