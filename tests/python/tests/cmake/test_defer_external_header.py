"""Verify the project marker header with both defer tool implementations."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[4]


@pytest.mark.parametrize("tool_variable", ["DEFER_TEST_BINARY", "DEFER_EXTERNAL_TEST_BINARY"])
def test_project_header_is_accepted_by_defer_tool(tmp_path: Path, tool_variable: str) -> None:
    tool = os.environ.get(tool_variable)
    compiler = shutil.which("clang")
    if not tool or not compiler:
        pytest.skip(f"{tool_variable} and Clang are required")
    source = tmp_path / "source.c"
    generated = tmp_path / "generated.c"
    source.write_text(
        '#include <recast/defer.h>\n'
        'int main(void) { int value = 0; { DEFER(value = 1); } return value != 1; }\n',
        encoding="utf-8",
    )
    include = str(ROOT / "deps" / "recast" / "cpp" / "include")
    transformed = subprocess.run([tool, str(source), str(generated), "--", "-I", include],
                                 capture_output=True, text=True, check=False)
    assert transformed.returncode == 0, transformed.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    subprocess.run([compiler, str(generated), "-I", include, "-o", str(executable)],
                   capture_output=True, text=True, check=True)
    subprocess.run([executable], check=True)
