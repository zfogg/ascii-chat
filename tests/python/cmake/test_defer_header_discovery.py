"""Verify defer header discovery prunes generated trees and remains incremental."""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]


def test_header_discovery_skips_environments_and_tracks_new_headers(tmp_path: Path) -> None:
    cmake, ninja = shutil.which("cmake"), shutil.which("ninja")
    if not cmake or not ninja:
        pytest.skip("CMake and Ninja are required")
    project = tmp_path / "project"
    headers = project / "tests" / "nested"
    headers.mkdir(parents=True)
    (headers / "wrapper.h").write_text("#define FIRST(action) defer(action)\n", encoding="utf-8")
    for name in (".venv", "venv", "node_modules", "__pycache__"):
        ignored = project / "tests" / name / "package"
        ignored.mkdir(parents=True)
        (ignored / "ignored.h").write_text(
            "#define IGNORED(action) defer(action)\nstatic void ignored(void) { defer((void)0); }\n",
            encoding="utf-8",
        )
    external = tmp_path / ".venv"
    external.mkdir(parents=True)
    (external / "external.h").write_text("#define EXTERNAL(action) defer(action)\n", encoding="utf-8")
    module = (ROOT / "cmake/tooling/Defer.cmake").as_posix()
    (project / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.22)\nproject(scan LANGUAGES NONE)\n"
        'set(ASCIICHAT_DEFER_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")\n'
        'set(ASCIICHAT_DEFER_SOURCE_DIRS tests CACHE STRING "")\n'
        f'set(ASCIICHAT_DEFER_WRAPPER_HEADER_DIRS "{external.as_posix()}" CACHE STRING "")\n'
        f'include("{module}")\nset(_batch_stage test)\n'
        "ascii_defer_scan_targets(selected)\n"
        "get_property(wrappers GLOBAL PROPERTY ASCII_DEFER_WRAPPER_NAMES)\n"
        "get_property(overlays GLOBAL PROPERTY ASCII_DEFER_HEADER_ACTIVE)\n"
        'file(WRITE "${CMAKE_BINARY_DIR}/scan-result.txt" "${wrappers}|${overlays}")\n',
        encoding="utf-8",
    )
    build = tmp_path / "build"

    def run(*args: str) -> str:
        result = subprocess.run(args, capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout

    run(cmake, "-S", str(project), "-B", str(build), "-G", "Ninja")
    result_file = build / "scan-result.txt"
    initial = result_file.read_text(encoding="utf-8")
    assert "FIRST" in initial and "EXTERNAL" in initial and "IGNORED" not in initial
    assert initial.endswith("|FALSE")

    new_dir = project / "tests" / "new" / "nested"
    new_dir.mkdir(parents=True)
    new_header = new_dir / "new.h"
    new_header.write_text(
        "#define SECOND(action) FIRST(action)\nstatic void f(void) { SECOND((void)0); }\n",
        encoding="utf-8",
    )
    run(cmake, "--build", str(build))
    updated = result_file.read_text(encoding="utf-8")
    assert "SECOND" in updated and updated.endswith("|TRUE")
    new_header.write_text("#define SECOND(action) ((void)0)\n", encoding="utf-8")
    run(cmake, "--build", str(build))
    assert "SECOND" not in result_file.read_text(encoding="utf-8")
    (project / "tests" / ".venv" / "package" / "another.h").write_text("ignored\n", encoding="utf-8")
    assert "Re-running CMake" not in run(cmake, "--build", str(build))
