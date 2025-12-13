# =============================================================================
# libsodium - System-installed library
# =============================================================================
# Links against system-installed libsodium via pkg-config or standard paths.
#
# Install on:
#   Arch: pacman -S libsodium
#   Debian/Ubuntu: apt install libsodium-dev
#   macOS: brew install libsodium
#   Fedora: dnf install libsodium-devel
#   Windows: vcpkg install libsodium

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "libsodium",
    hdrs = glob(
        ["root/include/sodium/**/*.h", "root/include/sodium.h"],
        allow_empty = True,
    ),
    includes = ["root/include"],
    linkopts = select({
        "@platforms//os:windows": [
            "-LIBPATH:root/lib",
            "libsodium.lib",
        ],
        "//conditions:default": ["-lsodium"],
    }),
    visibility = ["//visibility:public"],
)
