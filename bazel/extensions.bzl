# =============================================================================
# ascii-chat Module Extensions
# =============================================================================
# This file defines module extensions for dependencies not in the Bazel Central
# Registry (BCR).
#
# All BUILD files for vendored dependencies are kept in bazel/third_party/
# rather than in the deps/ submodules to avoid requiring forks.
# =============================================================================

def _is_windows(repository_ctx):
    """Check if running on Windows."""
    os_name = repository_ctx.os.name.lower()
    # Bazel may report "windows 10", "windows 11", "windows server", etc.
    return "windows" in os_name or os_name.startswith("win")

def _detect_system_lib_path(repository_ctx):
    """Detect the correct system library path based on platform."""
    # On Windows, use vcpkg installed path
    if _is_windows(repository_ctx):
        # Check common vcpkg locations
        vcpkg_root = repository_ctx.os.environ.get("VCPKG_ROOT", "")
        if not vcpkg_root:
            # Try common vcpkg paths on Windows
            vcpkg_root = "C:/vcpkg"
        triplet = repository_ctx.os.environ.get("VCPKG_TARGET_TRIPLET", "x64-windows")
        vcpkg_installed = vcpkg_root + "/installed/" + triplet

        # Verify the path exists using cmd
        result = repository_ctx.execute(["cmd", "/c", "if exist \"" + vcpkg_installed + "\" echo exists"])
        if result.return_code == 0 and "exists" in result.stdout:
            return vcpkg_installed

        # Fallback: just return the vcpkg path even if it doesn't exist
        # The BUILD file will handle missing libraries
        return vcpkg_installed

    # Check for macOS Homebrew paths first
    # Apple Silicon: /opt/homebrew
    # Intel Mac: /usr/local
    result = repository_ctx.execute(["uname", "-s"])
    if result.return_code == 0 and result.stdout.strip() == "Darwin":
        # On macOS, prefer Homebrew paths
        for path in ["/opt/homebrew", "/usr/local"]:
            check = repository_ctx.execute(["test", "-d", path + "/include"])
            if check.return_code == 0:
                return path
    # Default to /usr for Linux and other systems
    return "/usr"

def _system_lib_repo_impl(repository_ctx):
    """Repository rule for system libraries."""
    # Use auto-detected path if attr.path is "auto", otherwise use specified path
    if repository_ctx.attr.path == "auto":
        path = _detect_system_lib_path(repository_ctx)
    else:
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
    # Use platform-appropriate command
    if _is_windows(repository_ctx):
        # Windows: use cmd /c dir /b
        result = repository_ctx.execute(["cmd", "/c", "dir", "/b", str(src_path)])
    else:
        # Unix: use ls -A
        result = repository_ctx.execute(["ls", "-A", str(src_path)])

    if result.return_code != 0:
        fail("Failed to list directory: " + repository_ctx.attr.path)

    for item in result.stdout.strip().split("\n"):
        # Handle Windows CRLF line endings
        item = item.strip().replace("\r", "")
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

    # zstd - Compression library (system library)
    # "auto" detects the correct path: /opt/homebrew (macOS ARM), /usr/local (macOS Intel), /usr (Linux)
    _system_lib_repo(
        name = "zstd",
        path = "auto",
        build_file = "//bazel/third_party:zstd_system.BUILD",
    )

    # libsodium - Cryptography (system library)
    # "auto" detects the correct path: /opt/homebrew (macOS ARM), /usr/local (macOS Intel), /usr (Linux)
    _system_lib_repo(
        name = "libsodium",
        path = "auto",
        build_file = "//bazel/third_party:libsodium_system.BUILD",
    )

    # PortAudio - Audio I/O (system library)
    _system_lib_repo(
        name = "portaudio",
        path = "auto",
        build_file = "//bazel/third_party:portaudio_system.BUILD",
    )

    # LLVM/Clang - System libraries for defer tool
    # "auto" detects: /opt/homebrew (macOS ARM), /usr/local (macOS Intel), /usr (Linux)
    _system_lib_repo(
        name = "llvm_clang",
        path = "auto",
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
