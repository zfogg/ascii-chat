# =============================================================================
# Criterion - C/C++ testing framework
# =============================================================================
# https://github.com/Snaipe/Criterion
#
# Note: Criterion has complex build requirements. This uses a pre-built
# approach or builds from source with meson via rules_foreign_cc.

load("@rules_cc//cc:defs.bzl", "cc_library")

# For now, assume system-installed Criterion or use rules_foreign_cc
# This is a placeholder that links against system Criterion
cc_library(
    name = "criterion",
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    linkopts = select({
        "@platforms//os:linux": ["-lcriterion"],
        "@platforms//os:macos": ["-lcriterion"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
