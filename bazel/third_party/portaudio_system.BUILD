# =============================================================================
# PortAudio - System-installed library
# =============================================================================
# Links against system-installed PortAudio.
#
# Install on:
#   Arch: pacman -S portaudio
#   Debian/Ubuntu: apt install libportaudio2 portaudio19-dev
#   macOS: brew install portaudio
#   Fedora: dnf install portaudio-devel
#   Windows: vcpkg install portaudio

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "portaudio",
    hdrs = glob(
        ["root/include/portaudio.h"],
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
        "@platforms//os:linux": ["-lportaudio", "-lasound", "-lpthread"],
        "@platforms//os:macos": [
            "-lportaudio",
            "-framework CoreAudio",
            "-framework AudioToolbox",
            "-framework AudioUnit",
            "-framework CoreFoundation",
            "-framework CoreServices",
        ],
        "@platforms//os:windows": [
            "-LIBPATH:root/lib",
            "portaudio.lib",
        ],
        "//conditions:default": ["-lportaudio"],
    }),
    visibility = ["//visibility:public"],
)
