# =============================================================================
# ascii-chat Bazel WORKSPACE
# =============================================================================
# Legacy WORKSPACE file for compatibility with non-Bzlmod builds.
# Primary configuration is in MODULE.bazel (Bzlmod).
#
# To use WORKSPACE instead of Bzlmod:
#   bazel build --noenable_bzlmod //:ascii-chat
# =============================================================================

workspace(name = "ascii-chat")

# =============================================================================
# HTTP Archive Rule
# =============================================================================

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# =============================================================================
# rules_cc
# =============================================================================

http_archive(
    name = "rules_cc",
    sha256 = "abc605dd850f813bb37004b77db20106a19311a96b2da1c92b789da529d28fe1",
    strip_prefix = "rules_cc-0.0.17",
    urls = ["https://github.com/bazelbuild/rules_cc/releases/download/0.0.17/rules_cc-0.0.17.tar.gz"],
)

# =============================================================================
# platforms
# =============================================================================

http_archive(
    name = "platforms",
    sha256 = "218efe8ee736d26a3572663b374a253c012b716d8af0c07e842e82f238a0a7ee",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/platforms/releases/download/0.0.10/platforms-0.0.10.tar.gz",
        "https://github.com/bazelbuild/platforms/releases/download/0.0.10/platforms-0.0.10.tar.gz",
    ],
)

# =============================================================================
# rules_foreign_cc (for CMake/Make deps)
# =============================================================================
# Using bazel-contrib version which is actively maintained for newer Bazel

http_archive(
    name = "rules_foreign_cc",
    strip_prefix = "rules_foreign_cc-0.15.1",
    urls = ["https://github.com/bazel-contrib/rules_foreign_cc/releases/download/0.15.1/rules_foreign_cc-0.15.1.tar.gz"],
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")
rules_foreign_cc_dependencies(
    register_built_tools = False,
    register_default_tools = False,
    register_preinstalled_tools = True,
)

# =============================================================================
# rules_java (pinned for Bazel 8 compatibility)
# =============================================================================
# Must be loaded before rules_pkg to avoid version conflicts

http_archive(
    name = "rules_java",
    urls = ["https://github.com/bazelbuild/rules_java/releases/download/9.3.0/rules_java-9.3.0.tar.gz"],
    sha256 = "6ef26d4f978e8b4cf5ce1d47532d70cb62cd18431227a1c8007c8f7843243c06",
)

load("@rules_java//java:repositories.bzl", "rules_java_dependencies")
rules_java_dependencies()

# =============================================================================
# rules_pkg (for packaging)
# =============================================================================

http_archive(
    name = "rules_pkg",
    urls = [
        "https://github.com/bazelbuild/rules_pkg/releases/download/1.0.1/rules_pkg-1.0.1.tar.gz",
    ],
    sha256 = "d20c951960ed77cb7b341c2a59488534e494d5ad1d30c4818c736d57772a9fef",
)

load("@rules_pkg//:deps.bzl", "rules_pkg_dependencies")
rules_pkg_dependencies()

# =============================================================================
# External Dependencies
# =============================================================================

# zstd - Compression
http_archive(
    name = "zstd",
    build_file = "//bazel/third_party:zstd.BUILD",
    sha256 = "9c4396cc829cfae319a6e2615202e82aad41372073482fce286fac78646d3ee4",
    strip_prefix = "zstd-1.5.5",
    urls = ["https://github.com/facebook/zstd/releases/download/v1.5.5/zstd-1.5.5.tar.gz"],
)

# libsodium - Cryptography
# Note: libsodium requires configure/make - use rules_foreign_cc or system package
# For now, assume system-installed libsodium via pkg-config
new_local_repository(
    name = "libsodium",
    build_file = "//bazel/third_party:libsodium_system.BUILD",
    path = "/usr",
)

# PortAudio - Audio I/O
# Note: PortAudio requires configure/make - use system package
new_local_repository(
    name = "portaudio",
    build_file = "//bazel/third_party:portaudio_system.BUILD",
    path = "/usr",
)

# BearSSL - TLS
# Use the vendored copy in deps/bearssl
local_repository(
    name = "bearssl",
    path = "deps/bearssl",
)

# uthash - Header-only hash tables (vendored)
local_repository(
    name = "uthash",
    path = "deps/uthash",
)
