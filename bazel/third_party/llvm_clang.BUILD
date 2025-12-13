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
    hdrs = glob([
        "include/llvm/**/*.h",
        "include/llvm/**/*.def",
        "include/llvm/**/*.inc",
        "include/llvm-c/**/*.h",
    ]),
    includes = ["include"],
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
    hdrs = glob([
        "include/clang/**/*.h",
        "include/clang/**/*.def",
        "include/clang-c/**/*.h",
    ]),
    includes = ["include"],
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
