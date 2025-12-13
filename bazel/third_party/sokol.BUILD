# =============================================================================
# sokol - Minimal cross-platform header-only libraries
# =============================================================================

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "sokol",
    hdrs = glob(["*.h"]),
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "sokol_time",
    hdrs = ["sokol_time.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)
