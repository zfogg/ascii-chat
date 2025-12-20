# =============================================================================
# libsodium-bcrypt-pbkdf - OpenBSD bcrypt implementation
# =============================================================================
# Vendored dependency for bcrypt key derivation

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "libsodium_bcrypt_pbkdf",
    srcs = [
        "src/openbsd-compat/bcrypt_pbkdf.c",
        "src/openbsd-compat/blowfish.c",
        "src/sodium_bcrypt_pbkdf.c",
    ],
    hdrs = glob([
        "include/**/*.h",
        "src/**/*.h",
    ]),
    copts = [
        "-Wno-sizeof-array-div",  # Suppress warning in third-party code
        "-Wno-unterminated-string-initialization",  # Intentional 32-byte magic string
    ],
    includes = [
        "include",
        "src",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@libsodium//:libsodium",
    ],
)
