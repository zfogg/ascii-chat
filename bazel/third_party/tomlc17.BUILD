# =============================================================================
# tomlc17 - TOML parser in C
# =============================================================================
# https://github.com/arp242/toml-c

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "tomlc17",
    srcs = ["src/tomlc17.c"],
    hdrs = ["src/tomlc17.h"],
    copts = [
        "-Wno-unused-parameter",
        # Disable sanitizers - tomlc17 has memory leak reports in its pool allocator
        # that are not actionable (the pool is freed on toml_free)
        "-fno-sanitize=all",
    ],
    includes = ["src"],
    visibility = ["//visibility:public"],
)
