# =============================================================================
# ascii-chat Module Extensions
# =============================================================================
# This file defines module extensions for dependencies not in the Bazel Central
# Registry (BCR).
#
# All BUILD files for vendored dependencies are kept in bazel/third_party/
# rather than in the deps/ submodules to avoid requiring forks.
# =============================================================================

def _zstd_repo_impl(repository_ctx):
    """Repository rule for zstd."""
    repository_ctx.download_and_extract(
        url = "https://github.com/facebook/zstd/releases/download/v1.5.5/zstd-1.5.5.tar.gz",
        sha256 = "9c4396cc829cfae319a6e2615202e82aad41372073482fce286fac78646d3ee4",
        stripPrefix = "zstd-1.5.5",
    )
    repository_ctx.symlink(
        repository_ctx.path(Label("//bazel/third_party:zstd.BUILD")),
        "BUILD.bazel",
    )

_zstd_repo = repository_rule(
    implementation = _zstd_repo_impl,
)

def _system_lib_repo_impl(repository_ctx):
    """Repository rule for system libraries."""
    path = repository_ctx.attr.path
    build_file = repository_ctx.attr.build_file
    repository_ctx.symlink(path, "root")
    repository_ctx.symlink(
        repository_ctx.path(Label(build_file)),
        "BUILD.bazel",
    )

_system_lib_repo = repository_rule(
    implementation = _system_lib_repo_impl,
    attrs = {
        "path": attr.string(mandatory = True),
        "build_file": attr.string(mandatory = True),
    },
)

def _local_repo_impl(repository_ctx):
    """Repository rule for local/vendored dependencies.

    Symlinks source files from deps/ but uses BUILD file from bazel/third_party/.
    This avoids needing to fork submodules just to add BUILD files.
    """
    # Get the source path
    src_path = repository_ctx.path(Label("@//:WORKSPACE.bzlmod")).dirname.get_child(repository_ctx.attr.path)

    # Read the source directory and symlink each item, skipping BUILD files
    result = repository_ctx.execute(["ls", "-A", str(src_path)])
    if result.return_code != 0:
        fail("Failed to list directory: " + repository_ctx.attr.path)

    for item in result.stdout.strip().split("\n"):
        if item and item not in ["BUILD", "BUILD.bazel"]:
            repository_ctx.symlink(src_path.get_child(item), item)

    # Symlink the external BUILD file from bazel/third_party/
    repository_ctx.symlink(
        repository_ctx.path(Label(repository_ctx.attr.build_file)),
        "BUILD.bazel",
    )

_local_repo = repository_rule(
    implementation = _local_repo_impl,
    attrs = {
        "path": attr.string(mandatory = True),
        "build_file": attr.string(mandatory = True),
    },
    local = True,
)

def _non_bcr_deps_impl(module_ctx):
    """Implementation of non_bcr_deps module extension."""

    # zstd - Compression library (downloaded)
    _zstd_repo(name = "zstd")

    # libsodium - Cryptography (system library)
    _system_lib_repo(
        name = "libsodium",
        path = "/usr",
        build_file = "//bazel/third_party:libsodium_system.BUILD",
    )

    # PortAudio - Audio I/O (system library)
    _system_lib_repo(
        name = "portaudio",
        path = "/usr",
        build_file = "//bazel/third_party:portaudio_system.BUILD",
    )

    # LLVM/Clang - System libraries for defer tool
    _system_lib_repo(
        name = "llvm_clang",
        path = "/usr/local",
        build_file = "//bazel/third_party:llvm_clang.BUILD",
    )

    # Vendored dependencies - BUILD files in bazel/third_party/, sources in deps/
    _local_repo(
        name = "uthash",
        path = "deps/uthash",
        build_file = "//bazel/third_party:uthash.BUILD",
    )
    _local_repo(
        name = "bearssl",
        path = "deps/bearssl",
        build_file = "//bazel/third_party:bearssl.BUILD",
    )
    _local_repo(
        name = "tomlc17",
        path = "deps/tomlc17",
        build_file = "//bazel/third_party:tomlc17.BUILD",
    )
    _local_repo(
        name = "bcrypt_pbkdf",
        path = "deps/libsodium-bcrypt-pbkdf",
        build_file = "//bazel/third_party:bcrypt_pbkdf.BUILD",
    )
    _local_repo(
        name = "sokol",
        path = "deps/sokol",
        build_file = "//bazel/third_party:sokol.BUILD",
    )

    return module_ctx.extension_metadata(
        reproducible = True,
        root_module_direct_deps = [
            "zstd",
            "libsodium",
            "portaudio",
            "llvm_clang",
            "uthash",
            "bearssl",
            "tomlc17",
            "bcrypt_pbkdf",
            "sokol",
        ],
        root_module_direct_dev_deps = [],
    )

non_bcr_deps = module_extension(
    implementation = _non_bcr_deps_impl,
)
