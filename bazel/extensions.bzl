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
    build_file = repository_ctx.attr.build_file

    # On Windows, use vcpkg-installed libraries or custom windows_path
    if _is_windows(repository_ctx):
        # Check for custom Windows path (e.g., for LLVM installed at C:\LLVM)
        if repository_ctx.attr.windows_path:
            windows_base = repository_ctx.attr.windows_path
            # Handle ARM64 vs x64 LLVM paths (ARM64 is at C:\LLVM-ARM64)
            if windows_base == "C:/LLVM":
                arch = repository_ctx.os.arch
                if arch == "aarch64" or arch == "arm64":
                    windows_base = "C:/LLVM-ARM64"
        else:
            # Default to vcpkg path for most libraries
            vcpkg_root = repository_ctx.os.environ.get("VCPKG_ROOT", "C:/vcpkg")
            triplet = repository_ctx.os.environ.get("VCPKG_TARGET_TRIPLET", "x64-windows")
            windows_base = vcpkg_root + "/installed/" + triplet

        # Create root directory structure
        repository_ctx.file("root/.keep", "# Windows library placeholder")

        # Try to symlink directories (Windows supports directory junctions)
        include_path = windows_base.replace("/", "\\") + "\\include"
        lib_path = windows_base.replace("/", "\\") + "\\lib"
        bin_path = windows_base.replace("/", "\\") + "\\bin"

        # Check if paths exist and create symlinks
        result = repository_ctx.execute(["cmd", "/c", "if exist \"" + include_path + "\" echo EXISTS"])
        if result.return_code == 0 and "EXISTS" in result.stdout:
            repository_ctx.symlink(windows_base + "/include", "root/include")
        else:
            repository_ctx.file("root/include/.keep", "# placeholder")

        result = repository_ctx.execute(["cmd", "/c", "if exist \"" + lib_path + "\" echo EXISTS"])
        if result.return_code == 0 and "EXISTS" in result.stdout:
            repository_ctx.symlink(windows_base + "/lib", "root/lib")
        else:
            repository_ctx.file("root/lib/.keep", "# placeholder")

        result = repository_ctx.execute(["cmd", "/c", "if exist \"" + bin_path + "\" echo EXISTS"])
        if result.return_code == 0 and "EXISTS" in result.stdout:
            repository_ctx.symlink(windows_base + "/bin", "root/bin")
        else:
            repository_ctx.file("root/bin/.keep", "# placeholder")

        repository_ctx.symlink(
            repository_ctx.path(Label(build_file)),
            "BUILD.bazel",
        )
    else:
        # Unix: symlink system paths directly
        if repository_ctx.attr.path == "auto":
            path = _detect_system_lib_path(repository_ctx)
        else:
            path = repository_ctx.attr.path
        repository_ctx.symlink(path, "root")
        repository_ctx.symlink(
            repository_ctx.path(Label(build_file)),
            "BUILD.bazel",
        )

_system_lib_repo = repository_rule(
    implementation = _system_lib_repo_impl,
    attrs = {
        "path": attr.string(mandatory = True),
        "windows_path": attr.string(mandatory = False, default = ""),
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
    # Use platform-appropriate command to list directory contents
    if _is_windows(repository_ctx):
        # Windows: use PowerShell Get-ChildItem (more reliable than cmd dir)
        # Convert path to Windows format
        win_path = str(src_path).replace("/", "\\")
        result = repository_ctx.execute([
            "powershell", "-NoProfile", "-Command",
            "(Get-ChildItem -Name -Path '{}')".format(win_path),
        ])
    else:
        # Unix: use ls -A
        result = repository_ctx.execute(["ls", "-A", str(src_path)])

    if result.return_code != 0:
        # Debug: print the error
        print("Failed to list directory: {} (stderr: {})".format(
            repository_ctx.attr.path,
            result.stderr,
        ))
        fail("Failed to list directory: " + repository_ctx.attr.path + " - " + result.stderr)

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

    # Note: LLVM/Clang for the defer tool is now provided by the hermetic
    # toolchains_llvm module (defined in MODULE.bazel), not a system library.

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
