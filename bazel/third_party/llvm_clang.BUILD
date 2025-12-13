# =============================================================================
# System LLVM/Clang Libraries
# =============================================================================
# Provides access to system-installed LLVM and Clang libraries for building
# tools like the defer() transformation tool.
#
# Requires: LLVM/Clang development packages installed (llvm-config available)
#
# Linux: sudo apt install llvm-dev libclang-dev
# macOS: brew install llvm

cc_library(
    name = "llvm",
    hdrs = glob(
        [
            "root/include/llvm/**/*.h",
            "root/include/llvm/**/*.def",
            "root/include/llvm/**/*.inc",
            "root/include/llvm-c/**/*.h",
        ],
        allow_empty = True,
    ),
    includes = ["root/include"],
    linkopts = select({
        "@platforms//os:linux": [
            "-lLLVM",
        ],
        "@platforms//os:macos": [
            "-L/usr/local/lib",
            "-L/opt/homebrew/lib",
            "-lLLVM",
            "-Wl,-rpath,/usr/local/lib",
            "-Wl,-rpath,/opt/homebrew/lib",
        ],
        "//conditions:default": ["-lLLVM"],
    }),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "clang",
    hdrs = glob(
        [
            "root/include/clang/**/*.h",
            "root/include/clang/**/*.def",
            "root/include/clang-c/**/*.h",
        ],
        allow_empty = True,
    ),
    includes = ["root/include"],
    linkopts = select({
        "@platforms//os:linux": [
            "-lclang-cpp",
        ],
        "@platforms//os:macos": [
            "-L/usr/local/lib",
            "-L/opt/homebrew/lib",
            "-lclang-cpp",
            "-Wl,-rpath,/usr/local/lib",
            "-Wl,-rpath,/opt/homebrew/lib",
        ],
        "//conditions:default": ["-lclang-cpp"],
    }),
    visibility = ["//visibility:public"],
    deps = [":llvm"],
)
