"""Exercise the native defer driver's compilation-database and batch contract."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture
def native_tool() -> Path:
    binary = os.environ.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not binary:
        pytest.skip("native defer binary was not supplied")
    path = Path(binary)
    assert path.is_file(), f"native defer binary does not exist: {path}"
    return path


def test_plan_accepts_relative_build_and_bare_output(native_tool: Path, tmp_path: Path) -> None:
    records = tmp_path / "records.txt"
    records.write_text("app\t/source.c\t/output.c\t\n", encoding="utf-8")
    result = subprocess.run(
        [str(native_tool), "--plan-records", str(records), "--plan-stage", "production",
         "--plan-output", "plan.cmake", "--build-dir", "build"],
        cwd=tmp_path, capture_output=True, text=True, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert (tmp_path / "build").as_posix() in (tmp_path / "plan.cmake").read_text(encoding="utf-8")


def test_batch_compilation_database_dependencies_and_incremental_output(
    native_tool: Path, tmp_path: Path
) -> None:
    clang = shutil.which("clang")
    if clang is None:
        pytest.skip("Clang is required")
    root = tmp_path / "source é 猫"
    root.mkdir()
    header = root / "value.h"
    header.write_text("#define VALUE 3\n", encoding="utf-8")
    first = root / "first.c"
    second = root / "second.c"
    first.write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n#include "value.h"\n'
        "static int x; int first(void) { defer(x = VALUE); return 7; }\n"
        "int cleaned(void) { return x; }\n",
        encoding="utf-8",
    )
    second.write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n'
        "int first(void); int cleaned(void);\n"
        "int main(void) { defer((void)0); return first() != 7 || cleaned() != 3; }\n",
        encoding="utf-8",
    )
    generated = [root / "first-generated.c", root / "second-generated.c"]
    database = [
        {
            "directory": str(root),
            "file": str(output),
            "arguments": [
                clang,
                "-std=c17",
                "-I",
                str(ROOT / "include"),
                "-c",
                "--",
                str(output),
            ],
        }
        for output in generated
    ]
    (root / "compile_commands.json").write_text(json.dumps(database), encoding="utf-8")
    manifest = root / "batch.txt"
    manifest.write_text(
        f"{first.as_posix()}\t{generated[0].as_posix()}\n"
        f"{second.as_posix()}\t{generated[1].as_posix()}\n",
        encoding="utf-8",
    )
    depfile = root / "batch.d"
    command = [
        str(native_tool),
        "--batch",
        str(manifest),
        "--compilation-database",
        str(root),
        "--depfile",
        str(depfile),
    ]
    configured_libclang = os.environ.get("LIBCLANG_LIBRARY_FILE")
    if configured_libclang:
        command.extend(["--libclang", configured_libclang])
    result = subprocess.run(
        command, capture_output=True, text=True, encoding="utf-8", check=False
    )
    assert result.returncode == 0, result.stderr
    assert all(path.is_file() for path in generated)
    dependencies = depfile.read_text(encoding="utf-8")
    assert "value.h" in dependencies
    assert "first.c" in dependencies and "second.c" in dependencies
    executable = root / ("program.exe" if os.name == "nt" else "program")
    compiled = subprocess.run(
        [
            clang,
            "-std=c17",
            "-I",
            str(ROOT / "include"),
            *map(str, generated),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=False,
    )
    assert compiled.returncode == 0, compiled.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stderr

    before = [path.stat().st_mtime_ns for path in generated]
    second.write_text(
        second.read_text(encoding="utf-8") + "\n/* changed */\n", encoding="utf-8"
    )
    result = subprocess.run(
        command, capture_output=True, text=True, encoding="utf-8", check=False
    )
    assert result.returncode == 0, result.stderr
    assert generated[0].stat().st_mtime_ns == before[0]
    assert generated[1].read_text(encoding="utf-8").endswith("/* changed */\n")


def test_invalid_batch_fails_without_creating_output(
    native_tool: Path, tmp_path: Path
) -> None:
    manifest = tmp_path / "batch.txt"
    manifest.write_text("not-a-source-output-pair\n", encoding="utf-8")
    result = subprocess.run(
        [str(native_tool), "--batch", str(manifest)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode != 0
    assert "defer-tool:" in result.stderr


def test_rejects_a_different_explicit_libclang(
    native_tool: Path, tmp_path: Path
) -> None:
    wrong = tmp_path / "not-libclang"
    wrong.write_bytes(b"not a compatible shared library")
    result = subprocess.run(
        [str(native_tool), "--libclang", str(wrong), "input.c", "output.c"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode != 0
    assert "--libclang differs" in result.stderr


@pytest.mark.parametrize("cross", [False, True], ids=["native", "cross-host-tool"])
def test_cmake_registers_and_runs_native_unit_tests(tmp_path: Path, cross: bool) -> None:
    cmake = shutil.which("cmake")
    clang = shutil.which("clang")
    clangxx = shutil.which("clang++")
    ctest = shutil.which("ctest")
    if not cmake or not ctest or not clang or not clangxx or not shutil.which("ninja"):
        pytest.skip("CMake, CTest, Ninja, and Clang are required")
    project = tmp_path / "project"
    native = project / "src" / "tooling" / "defer"
    native.mkdir(parents=True)
    for pattern in ("*.cpp", "*.h", "CMakeLists.txt"):
        for path in (ROOT / "src" / "tooling" / "defer").glob(pattern):
            shutil.copy2(path, native / path.name)
    deps = project / "cmake" / "deps"
    deps.mkdir(parents=True)
    shutil.copy2(ROOT / "cmake" / "deps" / "LLVMDevelopment.cmake", deps)
    (project / "src" / "CMakeLists.txt").write_text("", encoding="utf-8")
    module = (ROOT / "cmake" / "tooling" / "Defer.cmake").as_posix()
    (project / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.30)\nproject(native_test C CXX)\n"
        "set(BUILD_TESTS ON)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        f'include("{module}")\nadd_subdirectory(src)\nascii_defer_prepare()\nenable_testing()\n',
        encoding="utf-8",
    )
    build = project / "build"
    args = [
        cmake, "-S", str(project), "-B", str(build), "-G", "Ninja",
        f"-DCMAKE_C_COMPILER={clang}", f"-DCMAKE_CXX_COMPILER={clangxx}",
    ]
    if cross:
        system = {"win32": "Windows", "darwin": "Darwin"}.get(sys.platform, "Linux")
        args.append(f"-DCMAKE_SYSTEM_NAME={system}")
    configured = subprocess.run(args, capture_output=True, text=True, check=False)
    assert configured.returncode == 0, configured.stdout + configured.stderr
    discovery = subprocess.run([ctest, "--test-dir", str(build), "-N"], capture_output=True, text=True, check=False)
    assert discovery.returncode == 0, discovery.stderr
    for name in ("ascii-defer-text", "ascii-defer-clang", "ascii-defer-transform"):
        assert name in discovery.stdout
    targets = ["ascii-defer-host-build"] if cross else [
        "ascii-defer-native", "ascii-defer-text-test", "ascii-defer-clang-test", "ascii-defer-transform-test"
    ]
    compiled = subprocess.run([cmake, "--build", str(build), "--target", *targets], capture_output=True, text=True, check=False)
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    tested = subprocess.run([ctest, "--test-dir", str(build), "--output-on-failure"], capture_output=True, text=True, check=False)
    assert tested.returncode == 0, tested.stdout + tested.stderr
