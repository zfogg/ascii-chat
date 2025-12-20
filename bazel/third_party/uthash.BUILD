# =============================================================================
# uthash - Hash table macros for C structures
# =============================================================================
# https://github.com/troydhanson/uthash
# Header-only library

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "uthash",
    hdrs = [
        "include/uthash.h",
        "include/utlist.h",
        "include/utarray.h",
        "include/utstring.h",
        "include/utringbuffer.h",
    ],
    includes = ["include"],
    visibility = ["//visibility:public"],
)
