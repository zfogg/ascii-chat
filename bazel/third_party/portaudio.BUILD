# =============================================================================
# PortAudio - Cross-platform audio I/O library
# =============================================================================
# http://www.portaudio.com/
#
# Note: PortAudio requires platform-specific backends. This BUILD file
# configures the appropriate backend based on the target platform.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "portaudio",
    srcs = glob([
        "src/common/*.c",
    ]) + select({
        "@platforms//os:linux": glob([
            "src/os/unix/*.c",
            "src/hostapi/alsa/*.c",
        ]),
        "@platforms//os:macos": glob([
            "src/os/unix/*.c",
            "src/hostapi/coreaudio/*.c",
        ]),
        "@platforms//os:windows": glob([
            "src/os/win/*.c",
            "src/hostapi/wmme/*.c",
            "src/hostapi/wasapi/*.c",
        ]),
        "//conditions:default": [],
    }),
    hdrs = glob([
        "include/*.h",
        "src/common/*.h",
    ]) + select({
        "@platforms//os:linux": glob(["src/os/unix/*.h"]),
        "@platforms//os:macos": glob(["src/os/unix/*.h"]),
        "@platforms//os:windows": glob(["src/os/win/*.h"]),
        "//conditions:default": [],
    }),
    copts = select({
        "@platforms//os:linux": ["-DPA_USE_ALSA=1"],
        "@platforms//os:macos": ["-DPA_USE_COREAUDIO=1"],
        "@platforms//os:windows": ["-DPA_USE_WMME=1", "-DPA_USE_WASAPI=1"],
        "//conditions:default": [],
    }),
    includes = [
        "include",
        "src/common",
    ],
    linkopts = select({
        "@platforms//os:linux": ["-lasound", "-lpthread"],
        "@platforms//os:macos": [
            "-framework CoreAudio",
            "-framework AudioToolbox",
            "-framework AudioUnit",
            "-framework CoreFoundation",
            "-framework CoreServices",
        ],
        "@platforms//os:windows": [
            "-lwinmm",
            "-lole32",
        ],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
