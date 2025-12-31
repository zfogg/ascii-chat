# =============================================================================
# Module 2: Cryptography (stable - changes monthly)
# =============================================================================
# Cryptographic protocol and key management sources

set(CRYPTO_SRCS
    lib/crypto/crypto.c
    lib/crypto/keys.c
    lib/crypto/known_hosts.c
    lib/crypto/handshake/common.c
    lib/crypto/handshake/server.c
    lib/crypto/handshake/client.c
    lib/crypto/pem_utils.c
    # GPG module (refactored into gpg/ subdirectory)
    lib/crypto/gpg/agent.c
    lib/crypto/gpg/export.c
    lib/crypto/gpg/signing.c
    lib/crypto/gpg/verification.c
    lib/crypto/gpg/gpg_keys.c  # GPG key parsing
    # SSH module (refactored into ssh/ subdirectory)
    lib/crypto/ssh/ssh_agent.c
    lib/crypto/ssh/ssh_keys.c  # SSH key parsing
    # Key management (at crypto root)
    lib/crypto/https_keys.c    # GitHub/GitLab key fetching
    lib/crypto/keys_validation.c  # Key validation utilities
    # libsodium-bcrypt-pbkdf (OpenBSD implementation)
    deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
)

# Suppress specific Clang warnings for libsodium-bcrypt-pbkdf (third-party code)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(
        deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
        deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
        PROPERTIES
        COMPILE_FLAGS "-Wno-sizeof-array-div"
    )
endif()

# Disable static analyzers for third-party libsodium-bcrypt-pbkdf code
set_source_files_properties(
    deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
    deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/blowfish.c
    deps/libsodium-bcrypt-pbkdf/src/sodium_bcrypt_pbkdf.c
    PROPERTIES
    SKIP_LINTING ON
)
