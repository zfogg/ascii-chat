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
    ],
    includes = ["src"],
    visibility = ["//visibility:public"],
)
