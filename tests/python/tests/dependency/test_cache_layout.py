"""Keep dependency build artifacts and patch bookkeeping out of the cache root."""

from pathlib import Path
import os
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[4]
pytestmark = pytest.mark.unit


@pytest.mark.parametrize("architecture", ["amd64", "arm64"])
@pytest.mark.parametrize("explicit_root", [False, True])
def test_docker_cache_root(tmp_path, architecture, explicit_root):
    source = tmp_path / "source"
    source.mkdir()
    (source / "CMakeLists.txt").write_text(
        f'''cmake_minimum_required(VERSION 3.30)
project(docker_cache_root NONE)
include("{ROOT.as_posix()}/cmake/init/DependencyCache.cmake")
file(WRITE "${{CMAKE_BINARY_DIR}}/paths.txt"
    "${{ASCIICHAT_DEPS_CACHE_ROOT}}\n${{ASCIICHAT_DEPS_CACHE_DIR}}\n")
''', encoding="utf-8")
    command = ["cmake", "-S", str(source), "-B", str(tmp_path / "build"), "-DCMAKE_BUILD_TYPE=Debug"]
    expected = source / ".deps-cache" / "docker" / architecture
    if explicit_root:
        expected = tmp_path / "mounted cache"
        command.append(f"-DASCIICHAT_DEPS_CACHE_ROOT={expected}")
    subprocess.run(command, check=True, capture_output=True, text=True, env=dict(os.environ, DOCKER_OS=architecture))
    actual = (tmp_path / "build" / "paths.txt").read_text(encoding="utf-8").splitlines()
    assert actual == [expected.as_posix(), (expected / "Debug").as_posix()]


@pytest.mark.parametrize("layout", ["default", "flat", "normalized-flat", "custom"])
@pytest.mark.parametrize("partition", ["native", "docker/amd64", "docker/arm64", "wsl/Arch", "wsl/Ubuntu"])
def test_dependency_cache_layout(tmp_path, layout, partition):
    source = tmp_path / "source"
    source.mkdir()
    cache = tmp_path / "cache with spaces"
    if partition != "native":
        cache /= partition
    configured = {
        "default": "",
        "flat": str(cache),
        "normalized-flat": str(cache / "unused" / ".."),
        "custom": str(tmp_path / "parent cache" / "Debug"),
    }[layout]
    expected = Path(configured) if layout == "custom" else cache / "Debug"
    (source / "CMakeLists.txt").write_text(
        f'''cmake_minimum_required(VERSION 3.30)
project(cache_layout NONE)
include("{ROOT.as_posix()}/cmake/init/DependencyCache.cmake")
file(WRITE "${{CMAKE_BINARY_DIR}}/paths.txt"
    "${{ASCIICHAT_DEPS_CACHE_DIR}}\n${{FETCHCONTENT_BASE_DIR}}\n")
''', encoding="utf-8")
    command = [
        "cmake", "-S", str(source), "-B", str(tmp_path / "build"),
        "-DCMAKE_BUILD_TYPE=Debug", f"-DASCIICHAT_DEPS_CACHE_ROOT={cache}",
    ]
    if configured:
        command.append(f"-DASCIICHAT_DEPS_CACHE_DIR={configured}")
    subprocess.run(command, check=True, capture_output=True, text=True)
    # Reconfiguration must preserve the migrated path without the old -D override.
    subprocess.run(command[:5], check=True, capture_output=True, text=True)
    actual = (tmp_path / "build" / "paths.txt").read_text(encoding="utf-8").splitlines()
    assert actual == [expected.as_posix(), expected.as_posix()]


def test_patch_markers_are_grouped_and_revalidated(tmp_path):
    source = tmp_path / "source"
    dependency = source / "example-dependency"
    dependency.mkdir(parents=True)
    subprocess.run(["git", "init", "--quiet", str(dependency)], check=True)
    original = dependency / "example.txt"
    original.write_text("before\n", encoding="utf-8")
    patch = source / "change.patch"
    patch.write_text(
        "diff --git a/example.txt b/example.txt\n"
        "--- a/example.txt\n+++ b/example.txt\n@@ -1 +1 @@\n-before\n+after\n",
        encoding="utf-8",
    )
    (source / "CMakeLists.txt").write_text(
        f'''cmake_minimum_required(VERSION 3.30)
project(patch_layout NONE)
set(ASCIICHAT_DEPS_CACHE_ROOT "${{CMAKE_BINARY_DIR}}/cache")
include("{ROOT.as_posix()}/cmake/utils/Patching.cmake")
apply_patch(TARGET_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}/example-dependency"
    PATCH_FILE "${{CMAKE_CURRENT_SOURCE_DIR}}/change.patch"
    PATCH_NUM 0 DESCRIPTION "Fixture patch")
''', encoding="utf-8")
    build = tmp_path / "build"
    command = ["cmake", "-S", str(source), "-B", str(build)]
    for _ in range(2):
        subprocess.run(command, check=True, capture_output=True, text=True)
        assert original.read_text(encoding="utf-8") == "after\n"
        marker = build / "cache" / "patches" / "example-dependency" / "patch_0"
        assert marker.is_file()
        assert not list((build / "cache").glob("*.patch_*"))
        # A marker must not hide a reset/replaced dependency source.
        original.write_text("before\n", encoding="utf-8")
