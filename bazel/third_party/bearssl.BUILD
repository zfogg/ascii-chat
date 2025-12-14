# =============================================================================
# BearSSL - Smaller SSL/TLS library
# =============================================================================
# https://bearssl.org/
#
# Note: BearSSL uses a Makefile. This BUILD file compiles the sources directly.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "bearssl",
    srcs = glob([
        "src/*.c",
        "src/**/*.c",
    ]),
    hdrs = glob([
        "inc/*.h",
        "src/*.h",
    ]),
    copts = [
        "-Wno-unused-parameter",
        # Disable sanitizers - BearSSL has known UBSan issues (unsigned negation)
        # that are benign but trigger sanitizer errors
        "-fno-sanitize=all",
    ],
    includes = [
        "inc",
        "src",  # For inner.h used by src/**/*.c
    ],
    local_defines = select({
        "@platforms//os:linux": ["_GNU_SOURCE"],  # For getentropy()
        "@platforms//os:macos": ["_DARWIN_C_SOURCE"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
