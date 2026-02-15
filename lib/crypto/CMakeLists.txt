# lib/crypto - Cryptography

set(CRYPTO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/keys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/known_hosts.c
    ${CMAKE_CURRENT_SOURCE_DIR}/regex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pem_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sha1.c
    ${CMAKE_CURRENT_SOURCE_DIR}/https_keys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery_keys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/keys_validation.c
    # handshake/
    ${CMAKE_CURRENT_SOURCE_DIR}/handshake/common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/handshake/server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/handshake/client.c
    # gpg/
    ${CMAKE_CURRENT_SOURCE_DIR}/gpg/agent.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gpg/export.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gpg/signing.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gpg/verification.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gpg/gpg_keys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gpg/openpgp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gpg/homedir.c
    # ssh/
    ${CMAKE_CURRENT_SOURCE_DIR}/ssh/ssh_agent.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ssh/ssh_keys.c
    # Third-party: libsodium-bcrypt-pbkdf
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
)

# Third-party code properties
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    PROPERTIES COMPILE_FLAGS "-Wno-sizeof-array-div"
    DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/lib/crypto/sha1.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
    PROPERTIES SKIP_LINTING ON
    DIRECTORY ${CMAKE_SOURCE_DIR}
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/lib/crypto/sha1.c
    PROPERTIES COMPILE_FLAGS "-fno-sanitize=all"
    DIRECTORY ${CMAKE_SOURCE_DIR}
)

set(CRYPTO_SRCS ${CRYPTO_SRCS} PARENT_SCOPE)
