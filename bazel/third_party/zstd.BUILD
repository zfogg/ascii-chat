# =============================================================================
# zstd - Fast real-time compression algorithm
# =============================================================================
# https://github.com/facebook/zstd

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "zstd",
    srcs = glob([
        "lib/common/*.c",
        "lib/compress/*.c",
        "lib/decompress/*.c",
    ]) + select({
        "@platforms//cpu:x86_64": ["lib/decompress/huf_decompress_amd64.S"],
        "//conditions:default": [],
    }),
    hdrs = glob([
        "lib/*.h",
        "lib/common/*.h",
        "lib/compress/*.h",
        "lib/decompress/*.h",
    ]),
    includes = ["lib"],
    local_defines = [
        "ZSTD_MULTITHREAD",
        "XXH_NAMESPACE=ZSTD_",
    ],
    visibility = ["//visibility:public"],
)
