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
# Windows: vovkos/llvm-package-windows installed at C:\LLVM

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
        # Windows: Link against LLVM libraries from vovkos package
        "@platforms//os:windows": [
            "-LIBPATH:root/lib",
            "LLVMSupport.lib",
            "LLVMCore.lib",
            "LLVMBinaryFormat.lib",
            "LLVMRemarks.lib",
            "LLVMBitstreamReader.lib",
            "LLVMDemangle.lib",
            "LLVMTargetParser.lib",
            "LLVMFrontendOpenMP.lib",
            "LLVMFrontendOffloading.lib",
            "LLVMOption.lib",
            "LLVMProfileData.lib",
            "LLVMMCParser.lib",
            "LLVMMC.lib",
            "LLVMDebugInfoCodeView.lib",
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
        # Windows: Link against Clang libraries from vovkos package
        "@platforms//os:windows": [
            "-LIBPATH:root/lib",
            "clangTooling.lib",
            "clangToolingCore.lib",
            "clangToolingInclusions.lib",
            "clangFrontend.lib",
            "clangDriver.lib",
            "clangParse.lib",
            "clangSema.lib",
            "clangAnalysis.lib",
            "clangAST.lib",
            "clangASTMatchers.lib",
            "clangEdit.lib",
            "clangLex.lib",
            "clangBasic.lib",
            "clangRewrite.lib",
            "clangSerialization.lib",
        ],
        "//conditions:default": ["-lclang-cpp"],
    }),
    visibility = ["//visibility:public"],
    deps = [":llvm"],
)
