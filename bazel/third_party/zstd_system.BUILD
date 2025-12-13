# =============================================================================
# zstd - System-installed library
# =============================================================================
# Links against system-installed zstd via pkg-config or standard paths.
#
# Install on:
#   Arch: pacman -S zstd
#   Debian/Ubuntu: apt install libzstd-dev
#   macOS: brew install zstd
#   Fedora: dnf install libzstd-devel

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "zstd",
    hdrs = glob(
        ["root/include/zstd*.h"],
        allow_empty = True,
    ),
    includes = ["root/include"],
    linkopts = ["-lzstd"],
    visibility = ["//visibility:public"],
)
