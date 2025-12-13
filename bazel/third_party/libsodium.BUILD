# =============================================================================
# libsodium - Modern cryptographic library
# =============================================================================
# https://github.com/jedisct1/libsodium
#
# Note: This is a simplified BUILD file. For full functionality, consider using
# rules_foreign_cc to build with autoconf/configure.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "libsodium",
    srcs = glob(
        [
            "src/libsodium/**/*.c",
            "src/libsodium/**/*.h",
        ],
        exclude = [
            "src/libsodium/**/*_sse*.c",
            "src/libsodium/**/*_avx*.c",
            "src/libsodium/**/*_ssse3*.c",
            "src/libsodium/**/aead_aes256gcm_aesni.c",
        ],
    ),
    hdrs = glob(["src/libsodium/include/**/*.h"]),
    copts = [
        "-DSODIUM_STATIC=1",
        "-DHAVE_WEAK_SYMBOLS=1",
        "-DCONFIGURED=1",
    ],
    includes = [
        "src/libsodium/include",
        "src/libsodium/include/sodium",
    ],
    local_defines = ["SODIUM_STATIC"],
    visibility = ["//visibility:public"],
)
