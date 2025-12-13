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
#   Windows: vcpkg install zstd

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "zstd",
    hdrs = glob(
        ["root/include/zstd*.h"],
        allow_empty = True,
    ),
    includes = ["root/include"],
    linkopts = select({
        "@platforms//os:windows": [
            "-LIBPATH:root/lib",
            "zstd.lib",
        ],
        "//conditions:default": ["-lzstd"],
    }),
    visibility = ["//visibility:public"],
)
