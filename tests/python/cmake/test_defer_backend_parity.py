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
        "void f(int x) { defer_named(cleanup, x++); if (x) defer_cancel(cleanup); }\n",
        "void f(void) { SAFE_MALLOC_DEFER_FREE(p, 4, char *); SAFE_CALLOC_DEFER_FREE(q, 2, 4, char *); (void)p; (void)q; }\n",
        "#define FIRST(x) defer(x++)\n#define SECOND(x) FIRST(x)\nvoid f(void) { int n = 1; SECOND(n); }\n",
        "#define FIRST(...) defer(__VA_ARGS__)\n#define SECOND(...) FIRST(__VA_ARGS__)\nvoid f(void) { int n = 1; SECOND(n++); }\n",
    ],
)
def test_generated_c_matches_python(
    native_binary: Path, tmp_path: Path, source: str
) -> None:
    source_path = tmp_path / "input.c"
    source_path.write_text(
        "#define defer(...) do {} while (0)\n"
        "#define defer_named(name, ...) do {} while (0)\n"
        "#define defer_cancel(name) do {} while (0)\n"
        "#define SAFE_MALLOC(size, cast) ((cast)0)\n"
        "#define SAFE_CALLOC(count, size, cast) ((cast)0)\n"
        "#define SAFE_FREE(p) do { (p) = 0; } while (0)\n"
        "#define SAFE_MALLOC_DEFER_FREE(name, size, cast) cast name = SAFE_MALLOC(size, cast); defer(SAFE_FREE(name))\n"
        "#define SAFE_CALLOC_DEFER_FREE(name, count, size, cast) cast name = SAFE_CALLOC(count, size, cast); defer(SAFE_FREE(name))\n"
        + source, encoding="utf-8"
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


@pytest.mark.parametrize(
    ("source", "diagnostic"),
    [
        ("void f(int x) { if (x) defer((void)0); }", "add braces"),
        ("void f(int x) { switch(x) { case 0: { defer((void)0); case 1: break; } } }", "switch dispatch"),
        ("void f(void) { defer(return); }", "control transfer"),
    ],
)
def test_unsafe_programs_fail_in_both_backends(
    native_binary: Path, tmp_path: Path, source: str, diagnostic: str
) -> None:
    source_path = tmp_path / "input.c"
    source_path.write_text("#define defer(...) do {} while (0)\n" + source + "\n", encoding="utf-8")
    for command in ([str(native_binary)], [sys.executable, str(PYTHON_TOOL)]):
        output = tmp_path / ("native.c" if len(command) == 1 else "python.c")
        result = subprocess.run(
            [*command, str(source_path), str(output), "--", "-std=c17"],
            capture_output=True, text=True, check=False,
        )
        assert result.returncode != 0
        assert diagnostic in result.stderr
        assert not output.exists()
