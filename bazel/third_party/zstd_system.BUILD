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
    # Include paths - root/include symlinks to system include dir
    # On macOS, we also need to check the direct Homebrew paths
    includes = ["root/include"] + select({
        "@platforms//os:macos": [
            "/opt/homebrew/include",  # macOS ARM64
            "/usr/local/include",     # macOS Intel
        ],
        "//conditions:default": [],
    }),
    linkopts = select({
        "@platforms//os:windows": [
            "-LIBPATH:root/lib",
            "zstd.lib",
        ],
        "//conditions:default": ["-lzstd"],
    }),
    visibility = ["//visibility:public"],
)
