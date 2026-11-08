"""Compare native and Python defer output for the same C translation units."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[3]
PYTHON_TOOL = ROOT / "src" / "tooling" / "defer" / "defer.py"


@pytest.fixture
def native_binary() -> Path:
    name = os.environ.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not name:
        pytest.skip("native defer binary was not supplied")
    path = Path(name)
    assert path.is_file()
    return path


@pytest.mark.parametrize(
    "source",
    [
        "void f(void) { int x = 1; defer(x++); }\n",
        "int f(int x) { defer(x++); { defer(x += 2); return x; } }\n",
        "void f(int x) { for (int i = 0; i < x; ++i) { defer(x++); if (i) break; continue; } }\n",
        "static int n; void f(void) { defer(n++); defer({ n += 2; }); }\n",
        "void f(int x) { { defer(x++); goto out; } out:; }\n",
        "struct S { int n; }; struct S f(int x) { struct S s = {x}; defer(x++); return s; }\n",
        "void f(void) { defer /* cleanup (LIFO) */ ((void)0); }\n",
        "int f(void) { defer((void)0); return\n__LINE__; }\n",
    ],
)
def test_generated_c_matches_python(
    native_binary: Path, tmp_path: Path, source: str
) -> None:
    source_path = tmp_path / "input.c"
    source_path.write_text(
        "#define defer(...) do {} while (0)\n" + source, encoding="utf-8"
    )
    native_output = tmp_path / "native.c"
    python_output = tmp_path / "python.c"
    options = ["--", "-std=c17"]
    native = subprocess.run(
        [str(native_binary), str(source_path), str(native_output), *options],
        capture_output=True,
        text=True,
        check=False,
    )
    python = subprocess.run(
        [
            sys.executable,
            str(PYTHON_TOOL),
            str(source_path),
            str(python_output),
            *options,
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    assert python.returncode == 0, python.stderr
    assert native.returncode == 0, native.stderr
    assert native_output.read_bytes() == python_output.read_bytes()
