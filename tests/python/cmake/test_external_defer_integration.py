"""Exercise ascii-chat's standalone defer CMake integration."""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]


def test_external_defer_incremental_addition(tmp_path: Path) -> None:
    cmake, clang, ninja = (shutil.which(name) for name in ("cmake", "clang", "ninja"))
    binary = os.environ.get("DEFER_TEST_BINARY")
    if not cmake or not clang or not ninja or not binary:
        pytest.skip("CMake, Ninja, Clang and DEFER_TEST_BINARY are required")
    project = tmp_path / "project"
    source_dir = project / "src"
    source_dir.mkdir(parents=True)
    include_dir = project / "private-headers"
    include_dir.mkdir()
    (include_dir / "config.h").write_text('#define CLEANUP_TEXT "cleaned"\n', encoding="utf-8")
    header = ROOT / "include" / "ascii-chat" / "tooling" / "defer" / "defer.h"
    shutil.copy2(header, source_dir / "defer.h")
    source = source_dir / "main.c"
    source.write_text("int main(void) { return 0; }\n", encoding="utf-8")
    (source_dir / "CMakeLists.txt").write_text(
        'add_executable(ascii-chat-probe main.c)\n'
        'target_include_directories(ascii-chat-probe PRIVATE "${CMAKE_SOURCE_DIR}/private-headers")\n'
        'target_compile_definitions(ascii-chat-probe PRIVATE REQUIRE_DEFINE=1)\n'
        'set_property(TARGET ascii-chat-probe PROPERTY C_STANDARD 17)\n', encoding="utf-8"
    )
    module = (ROOT / "deps" / "defer" / "Defer.cmake").as_posix()
    (project / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.30)\nproject(probe C)\n"
        "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        f'include("{module}")\nadd_subdirectory(src)\ndefer_targets(ascii-chat-probe)\n',
        encoding="utf-8",
    )
    build = tmp_path / "build"

    def run(*args: str) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(args, capture_output=True, text=True, check=False, timeout=180)
        assert result.returncode == 0, result.stdout + result.stderr
        return result

    run(cmake, "-S", str(project), "-B", str(build), "-G", "Ninja", f"-DCMAKE_C_COMPILER={clang}",
        f"-DDEFER_NATIVE_BINARY={binary}")
    run(cmake, "--build", str(build))
    source.write_text(
        '#include "defer.h"\n#include <stdio.h>\n#include <config.h>\n'
        '#if !REQUIRE_DEFINE || __STDC_VERSION__ != 201710L\n#error incorrect compiler flags\n#endif\n'
        'int main(void) { defer(puts(CLEANUP_TEXT)); return 0; }\n', encoding="utf-8"
    )
    run(cmake, "--build", str(build))
    executable = build / "src" / ("ascii-chat-probe.exe" if os.name == "nt" else "ascii-chat-probe")
    assert run(str(executable)).stdout.strip() == "cleaned"
    assert "Transforming defer source batch" not in run(cmake, "--build", str(build)).stdout
