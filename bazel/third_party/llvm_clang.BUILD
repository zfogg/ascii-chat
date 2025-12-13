# =============================================================================
# System LLVM/Clang Libraries
# =============================================================================
# Provides access to system-installed LLVM and Clang libraries for building
# tools like the defer() transformation tool.
#
# Requires: LLVM/Clang development packages installed (llvm-config available)

cc_library(
    name = "llvm",
    hdrs = glob(["root/include/llvm/**/*.h", "root/include/llvm/**/*.def", "root/include/llvm/**/*.inc", "root/include/llvm-c/**/*.h"]),
    includes = ["root/include"],
    linkopts = [
        "-L/usr/local/lib",
        "-lLLVM",
        "-Wl,-rpath,/usr/local/lib",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "clang",
    hdrs = glob(["root/include/clang/**/*.h", "root/include/clang/**/*.def", "root/include/clang-c/**/*.h"]),
    includes = ["root/include"],
    linkopts = [
        "-L/usr/local/lib",
        "-lclang-cpp",
        "-Wl,-rpath,/usr/local/lib",
    ],
    visibility = ["//visibility:public"],
    deps = [":llvm"],
)
