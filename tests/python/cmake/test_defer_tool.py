"""Compile and execute transformed C to check defer's control-flow contract."""

from __future__ import annotations

import json
import os
import random
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "src" / "tooling" / "defer" / "defer.py"
CLANG = shutil.which("clang")


@pytest.fixture
def base_environment() -> dict[str, str]:
    if CLANG is None:
        pytest.skip("Clang is not installed")
    return os.environ.copy()


@pytest.fixture
def compiler_environment(base_environment: dict[str, str]) -> dict[str, str]:
    env = base_environment.copy()
    assert CLANG is not None
    if not env.get("LIBCLANG_LIBRARY_FILE"):
        compiler = Path(CLANG).resolve()
        result = subprocess.run(
            [str(compiler), "-print-resource-dir"],
            capture_output=True,
            text=True,
            check=False,
        )
        resource_lib = (
            Path(result.stdout.strip()).parent.parent
            if result.returncode == 0
            else compiler.parent
        )
        name = (
            "libclang.dll"
            if sys.platform == "win32"
            else "libclang.dylib"
            if sys.platform == "darwin"
            else "libclang.so"
        )
        candidates = [
            compiler.parent / name,
            compiler.parent.parent / "lib" / name,
            resource_lib / name,
        ]
        library = next(
            (candidate.resolve() for candidate in candidates if candidate.is_file()),
            None,
        )
        if library is None:
            pytest.skip(
                "LLVM libclang shared library not found; set LIBCLANG_LIBRARY_FILE"
            )
        env["LIBCLANG_LIBRARY_FILE"] = str(library)
    return env


@pytest.fixture(params=["python", "native"])
def transform_environment(
    base_environment: dict[str, str], request: pytest.FixtureRequest
) -> dict[str, str]:
    if request.param == "native":
        env = base_environment.copy()
        if not env.get("ASCIICHAT_DEFER_TEST_BINARY"):
            pytest.skip("native defer binary was not supplied")
    else:
        env = request.getfixturevalue("compiler_environment").copy()
        env.pop("ASCIICHAT_DEFER_TEST_BINARY", None)
    return env


def tool_command(env: dict[str, str]) -> list[str]:
    binary = env.get("ASCIICHAT_DEFER_TEST_BINARY")
    return [binary] if binary else [sys.executable, str(TOOL)]


def test_explain_reports_nested_cleanup_without_writing(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = tmp_path / "input.c"
    source.write_text(
        "#define defer(...) do {} while (0)\n"
        "int f(int x) {\n"
        "  defer(x += 1);\n"
        "  {\n"
        "    defer(x += 2);\n"
        "    return x;\n"
        "  }\n"
        "}\n",
        encoding="utf-8",
    )
    result = subprocess.run(
        [*tool_command(transform_environment), "--explain", f"{source}:6", "--", "-std=c17"],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "only if registered" in result.stdout
    assert "1. x += 2 (registered at line 5)" in result.stdout
    assert "2. x += 1 (registered at line 3)" in result.stdout
    assert list(tmp_path.iterdir()) == [source]


def test_show_transformed_selects_function_without_writing(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = tmp_path / "input.c"
    source.write_text(
        "#define defer(...) do {} while (0)\n"
        "void other(void) {}\n"
        "int target(int x) {\n"
        "  defer(x++);\n"
        "  return x;\n"
        "}\n",
        encoding="utf-8",
    )
    result = subprocess.run(
        [*tool_command(transform_environment), "--show-transformed", f"{source}:5", "--", "-std=c17"],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "int target(int x)" in result.stdout
    assert "__ascii_defer_active_" in result.stdout
    assert "other(void)" not in result.stdout
    assert '#line 3 ' in result.stdout
    assert list(tmp_path.iterdir()) == [source]


def test_invalid_cleanup_reports_original_source_line(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(tmp_path, transform_environment, "void f(void) {\n  defer(missing_name++);\n}\n")
    assert result.returncode != 0
    assert "input.c:3:" in result.stderr.replace("\\", "/")
    assert "missing_name" in result.stderr
    assert not (tmp_path / "output.c").exists()


@pytest.mark.parametrize("location", ["missing", "file.c:0", "file.c:not-a-line"])
def test_explain_rejects_invalid_locations(
    tmp_path: Path, transform_environment: dict[str, str], location: str
) -> None:
    result = subprocess.run(
        [*tool_command(transform_environment), "--explain", location],
        capture_output=True, text=True, env=transform_environment, cwd=tmp_path, check=False,
    )
    assert result.returncode != 0
    assert "--explain" in result.stderr


@pytest.mark.parametrize("alias", ["source", "output"])
def test_depfile_cannot_replace_source_or_output(
    tmp_path: Path, transform_environment: dict[str, str], alias: str
) -> None:
    source = tmp_path / "input.c"
    output = tmp_path / "output.c"
    original = "#define defer(...) do {} while (0)\nvoid f(void) { defer((void)0); }\n"
    source.write_text(original, encoding="utf-8")
    output.write_text("keep output", encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), "--depfile", str(source if alias == "source" else output),
         str(source), str(output), "--", "-std=c17"],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert source.read_text(encoding="utf-8") == original
    assert output.read_text(encoding="utf-8") == "keep output"


def test_batch_depfile_cannot_replace_any_source(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    sources = [tmp_path / "a.c", tmp_path / "b.c"]
    for source in sources:
        source.write_text("void f(void) {}\n", encoding="utf-8")
    manifest = tmp_path / "batch.txt"
    manifest.write_text(
        "".join(f"{source}\t{tmp_path / (source.stem + '.out.c')}\n" for source in sources), encoding="utf-8"
    )
    command = [*tool_command(transform_environment), "--batch", str(manifest),
               "--compilation-database", str(tmp_path), "--depfile", str(sources[1])]
    if not transform_environment.get("ASCIICHAT_DEFER_TEST_BINARY"):
        command.extend(["--libclang", transform_environment["LIBCLANG_LIBRARY_FILE"]])
    result = subprocess.run(command, capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode != 0
    assert all(source.read_text(encoding="utf-8") == "void f(void) {}\n" for source in sources)
    assert not (tmp_path / "a.out.c").exists()


def test_parameter_shadowing_function_name(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(tmp_path, transform_environment, "int f(int f) { defer((void)0); return f; }\n")
    assert result.returncode == 0, result.stderr
    compiled = subprocess.run(
        [str(CLANG), "-std=c17", "-fsyntax-only", str(tmp_path / "output.c")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert compiled.returncode == 0, compiled.stderr


@pytest.mark.parametrize("parameter", ["int", "unsigned long", "double", "const int"])
def test_unnamed_builtin_parameter_with_defer(
    tmp_path: Path, transform_environment: dict[str, str], parameter: str
) -> None:
    result = transform(tmp_path, transform_environment, f"int f({parameter}) {{ defer((void)0); return 7; }}\n")
    assert result.returncode == 0, result.stderr
    result = subprocess.run(
        [str(CLANG), "-std=c17", "-fsyntax-only", str(tmp_path / "output.c")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr


def test_unnamed_pointer_parameter_fails_closed(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(tmp_path, transform_environment, "int f(int *) { defer((void)0); return 7; }\n")
    assert result.returncode != 0
    assert "unnamed non-builtin parameter" in result.stderr
    assert not (tmp_path / "output.c").exists()


def test_goto_into_block_without_defer_runs_exited_cleanup(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(
        tmp_path, transform_environment,
        "static int n; void f(void) { defer(n=n*10+1); { defer(n=n*10+2); goto target; } "
        "{ target: n=n*10+3; } } int main(void) { f(); return n != 231; }\n",
    )
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run(
        [str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_switch_cannot_enter_defer_scope(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(
        tmp_path, transform_environment,
        "void f(int n) { switch (n) { case 0: { defer((void)0); case 1: break; } } }\n",
    )
    assert result.returncode != 0
    assert "switch dispatch" in result.stderr


def test_spliced_defer_identifier(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(tmp_path, transform_environment, "void f(void) { de\\\nfer((void)0); }\n")
    assert result.returncode == 0, result.stderr
    compiled = subprocess.run(
        [str(CLANG), "-std=c17", "-fsyntax-only", str(tmp_path / "output.c")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert compiled.returncode == 0, compiled.stderr


def test_spliced_macro_control_transfer_is_rejected(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(
        tmp_path, transform_environment,
        "#define break_if(x) if (x) break\n"
        "void f(void) { while (1) { defer((void)0); break\\\n_if(0); break; } }\n",
    )
    assert result.returncode != 0


@pytest.mark.parametrize("seed", range(12))
def test_generated_nested_cleanup_semantics(
    tmp_path: Path, transform_environment: dict[str, str], seed: int
) -> None:
    rng = random.Random(seed)
    outer, inner, immediate, tail = rng.sample(range(1, 100), 4)
    expected: list[int] = []
    results = []
    for mode in range(4):
        if mode & 1:
            if mode & 2:
                expected.extend((inner, outer))
                results.append(7)
                continue
            expected.extend((immediate, inner))
        expected.extend((tail, outer))
        results.append(8)
    checks = "\n".join(f"if (trace[{i}] != {value}) return {i + 10};" for i, value in enumerate(expected))
    calls = "\n".join(f"if (f({i}) != {value}) return {i + 1};" for i, value in enumerate(results))
    source = f"""
static int trace[64], count;
static void mark(int value) {{ trace[count++] = value; }}
static int f(int mode) {{
    defer(mark({outer}));
    if (mode & 1) {{
        defer(mark({inner}));
        if (mode & 2) return 7;
        mark({immediate});
    }}
    mark({tail});
    return 8;
}}
int main(void) {{ {calls} if (count != {len(expected)}) return 9; {checks} return 0; }}
"""
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    exe = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    compiled = subprocess.run(
        [str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(exe)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert compiled.returncode == 0, compiled.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, f"seed={seed}, exit={run.returncode}: {run.stderr}"


@pytest.mark.parametrize("seed", range(12))
def test_loop_exits_match_reference_model(
    tmp_path: Path, transform_environment: dict[str, str], seed: int
) -> None:
    """Compare each backend's execution trace to a small independent model."""
    rng = random.Random(seed)
    outer, loop, branch = rng.sample(range(1, 50), 3)
    expected: list[int] = []
    for mode in range(4):
        for index in range(3):
            if index == 1:
                expected.append(branch + index)
                expected.append(loop + index)
                if mode == 0:
                    continue
                if mode == 1:
                    break
                if mode == 2:
                    expected.append(outer + index)
                    break
            else:
                expected.append(loop + index)
        else:
            index = 3
        if mode != 2:
            expected.append(outer + index)
    checks = "\n".join(f"if (trace[{i}] != {value}) return {i + 10};" for i, value in enumerate(expected))
    source = f"""
static int trace[64], count;
static void mark(int value) {{ trace[count++] = value; }}
static void f(int mode) {{
  int i = 0;
  defer(mark({outer} + i));
  for (; i < 3; ++i) {{
    defer(mark({loop} + i));
    if (i == 1) {{
      defer(mark({branch} + i));
      if (mode == 0) continue;
      if (mode == 1) break;
      if (mode == 2) return;
    }}
  }}
}}
int main(void) {{ for (int i = 0; i < 4; ++i) f(i); if (count != {len(expected)}) return 1; {checks} return 0; }}
"""
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    compiled = subprocess.run(
        [str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert compiled.returncode == 0, compiled.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, f"seed={seed}, exit={run.returncode}: {run.stderr}"


def test_diagnostic_identifies_location_and_remedy(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(tmp_path, transform_environment, "void f(int x) { if (x) defer((void)0); }\n")
    assert result.returncode != 0
    assert "input.c:2:" in result.stderr.replace("\\", "/")
    assert "add braces" in result.stderr


@pytest.mark.parametrize(
    ("source", "remedy"),
    [
        ("int x; void f(void) { defer(x++); int x = 2; (void)x; return; }", "rename the inner declaration"),
        ("#define VALUE 1\nint x; void f(void) { defer(x += VALUE);\n#undef VALUE\nreturn; }", "move the directive"),
    ],
)
def test_unsafe_cleanup_diagnostic_identifies_remedy(
    tmp_path: Path, transform_environment: dict[str, str], source: str, remedy: str
) -> None:
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode != 0
    assert "input.c:" in result.stderr.replace("\\", "/")
    assert remedy in result.stderr
    assert not (tmp_path / "output.c").exists()


def test_invalid_cleanup_is_rejected_before_writing_output(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(tmp_path, transform_environment, "void f(int x) { defer(x+,); }\n")
    assert result.returncode != 0
    assert "generated C is invalid" in result.stderr
    assert not (tmp_path / "output.c").exists()


@pytest.mark.parametrize(
    ("body", "expected"),
    [
        ("defer_named(h, n += 1);", 1),
        ("defer_named(h, n += 1); defer_cancel(h);", 0),
        ("defer_named(h, n += 1); if (1) defer_cancel(h);", 0),
        ("defer_named(h, n += 1); if (0) defer_cancel(h);", 1),
        ("defer_named(h, n = n * 10 + 1); defer(n = n * 10 + 2); defer_cancel(h);", 2),
        ("defer_named(h, n += 1); { defer_cancel(h); }", 0),
        ("for (int i=0; i<3; ++i) { defer_named(h, n += 1); if (i == 1) defer_cancel(h); }", 2),
        ("defer_named(h, n += 1); defer_cancel(h); defer_cancel(h);", 0),
        ("defer_named(h, n += 1); defer_cancel(h); return;", 0),
        ("defer_named(h, n += 1); return;", 1),
        ("defer_named(h, n += 1); { defer_named(inner, n += 2); defer_cancel(h); goto done; } done:;", 2),
        ("{ defer_named(h, n += 1); } { defer_named(h, n += 2); defer_cancel(h); }", 1),
    ],
)
def test_named_defer_cancellation(
    tmp_path: Path, transform_environment: dict[str, str], body: str, expected: int
) -> None:
    source = (
        "#define defer_named(name, ...) do {} while (0)\n"
        "#define defer_cancel(name) do {} while (0)\n"
        f"static int n; static void f(void) {{ {body} }}\n"
        f"int main(void) {{ f(); return n != {expected}; }}\n"
    )
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run(
        [str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


@pytest.mark.parametrize(
    "body",
    [
        "defer_cancel(missing);",
        "defer_cancel(h); defer_named(h, n++);",
        "defer_named(h, n++); defer_named(h, n++);",
        "{ defer_named(h, n++); } defer_cancel(h);",
        "defer_named(h + 1, n++);",
        "defer_named(h, defer_cancel(h));",
        "defer_named(h, n++); defer(CANCEL());",
    ],
)
def test_named_defer_rejects_unsafe_handles(
    tmp_path: Path, transform_environment: dict[str, str], body: str
) -> None:
    result = transform(
        tmp_path, transform_environment,
        "#define defer_named(name, ...) do {} while (0)\n"
        "#define defer_cancel(name) do {} while (0)\n"
        "#define CANCEL() defer_cancel(h)\n"
        f"static int n; void f(void) {{ {body} }}\n",
    )
    assert result.returncode != 0
    assert not (tmp_path / "output.c").exists()


@pytest.mark.parametrize("source", ["void f(void) { defer_named(h, (void)0); }", "void f(void) { defer_cancel(h); }"])
def test_cmake_scanners_find_named_defer(
    tmp_path: Path, base_environment: dict[str, str], source: str
) -> None:
    native_binary = base_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not native_binary:
        pytest.skip("native defer binary was not supplied")
    path = tmp_path / "input.c"
    path.write_text(source, encoding="utf-8")
    python = subprocess.run(
        [sys.executable, str(TOOL.parent / "scan.py")], input=str(path) + "\n",
        capture_output=True, text=True, check=False,
    )
    native = subprocess.run([native_binary, "--scan", str(path)], capture_output=True, text=True, check=False)
    assert python.returncode == native.returncode == 0
    assert Path(python.stdout.strip()).resolve() == path.resolve()
    assert Path(native.stdout.strip()).resolve() == path.resolve()


def test_named_defer_works_with_public_header(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = tmp_path / "public.c"
    output = tmp_path / "generated.c"
    source.write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n'
        'int main(void) { defer_named(cleanup, (void)0); defer_cancel(cleanup); return 0; }\n',
        encoding="utf-8",
    )
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-std=c17", f"-I{ROOT / 'include'}"],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run(
        [str(CLANG), "-std=c17", f"-I{ROOT / 'include'}", "-fsyntax-only", str(output)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr


def transform(
    tmp_path: Path, env: dict[str, str], source: str
) -> subprocess.CompletedProcess[str]:
    original = tmp_path / "input.c"
    original.write_text(
        "#define defer(...) do {} while (0)\n" + source, encoding="utf-8"
    )
    return subprocess.run(
        [
            *tool_command(env),
            str(original),
            str(tmp_path / "output.c"),
            "--",
            "-std=c17",
        ],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )


def test_block_scope_and_control_flow(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(
        tmp_path,
        transform_environment,
        r"""
static int trace[20], count;
static void record(int n) { trace[count++] = n; }
static int early(int v) {
    defer(record(v));
    { defer(record(2)); v = 3; return v; }
}
static int captured_return(void) { int value = 1; defer(value = 2); return value; }
static void skip_defer(void) { goto done; defer(record(99)); done:; }
int main(void) {
    skip_defer();
    if (count != 0 || captured_return() != 1) return 4;
    int x = early(1);
    if (x != 3 || count != 2 || trace[0] != 2 || trace[1] != 3) return 1;
    for (int i = 0; i < 3; i++) {
        defer(record(10 + i));
        if (i == 0) continue;
        if (i == 1) break;
    }
    if (count != 4 || trace[2] != 10 || trace[3] != 11) return 2;
    { defer /* cleanup (LIFO) */ (record(20)); defer({ int a = 20, b = 1; record(a + b); }); }
    { defer((void)0 // cleanup comment
      ); }
    { defer(record(22)); goto after_block; }
    after_block:;
    if (count != 7 || trace[4] != 21 || trace[5] != 20 || trace[6] != 22) return 3;
    return 0;
}
""",
    )
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("probe.exe" if sys.platform == "win32" else "probe")
    compiled = subprocess.run(
        [str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, f"exit {run.returncode}: {run.stderr}"


@pytest.mark.parametrize(
    "source",
    [
        "void f(int x) { again: defer(x++); if (x) goto again; }",
        "void f(int x) { switch(x) { case 0: ; defer(x++); break; case 1: break; } }",
        "#define ESC(x) if (x) goto out\nvoid f(int x) { { defer(x++); ESC(x); } out:; }",
        "#define ESC(x) if (x) break\nvoid f(int x) { while (x) { defer(x++); ESC(x); } }",
        "#define break_if(x) if (x) break\nvoid f(int x) { while (x) { defer(x++); break_if(x); } }",
        "#define return_if(x) if (x) return\nvoid f(int x) { defer(x++); return_if(x); }",
        "#define continue_if(x) if (x) continue\nvoid f(int x) { while (x) { defer(x++); continue_if(x); } }",
        "void f(int x) { if (x) defer(x++); }",
        "void f(int x) { defer({ if (x) return; }); }",
        "void f(int x) { goto inner; { inner: defer(x++); } }",
        "void f(int x) { defer(x++); void *p = &&done; goto *p; done:; }",
        "void f(void) { int x = 1; defer(x++); { int x = 2; (void)x; return; } }",
        "#define CLEAN() (x++)\nvoid f(void) { int x = 1; defer(CLEAN()); { int x = 2; (void)x; return; } }",
        "#define ESCAPE() return\nvoid f(void) { defer(ESCAPE()); }",
        "void f(void) { defer((void)__COUNTER__); }",
        '#line 100 "virtual.c"\nvoid f(void) { defer((void)0); }',
        "#define VALUE 1\nint x; void f(void) { defer(x += VALUE);\n#undef VALUE\n#define VALUE 2\nreturn; }",
        "int f(int n, int (*array)[n]) { defer((void)array); return n; }",
        "#define JOIN(a, b) a ## b\nvoid f(void) { int x = 1; defer(JOIN(x, x)); }",
        "int x; void f(void) { defer(x++); int x = 2; (void)x; return; }",
        "struct S { int a; }; void f(void) { defer((void)sizeof(struct S)); { struct S { char a; }; return; } }",
        "void f(void) { int i = 1; defer(i++); for (int i = 0; i < 1; ++i) { return; } }",
    ],
)
def test_unsupported_control_flow_fails_closed(
    tmp_path: Path,
    transform_environment: dict[str, str],
    source: str,
) -> None:
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode != 0
    assert not (tmp_path / "output.c").exists()
    assert "defer-tool:" in result.stderr


def test_untransformed_defer_fails_compilation(
    tmp_path: Path, compiler_environment: dict[str, str]
) -> None:
    source = tmp_path / "untransformed.c"
    source.write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\nvoid f(void) { defer((void)0); }\n',
        encoding="utf-8",
    )
    result = subprocess.run(
        [
            str(CLANG),
            "-std=c17",
            "-fsyntax-only",
            "-I",
            str(ROOT / "include"),
            str(source),
        ],
        capture_output=True,
        text=True,
        env=compiler_environment,
        check=False,
    )
    assert result.returncode != 0
    assert "defer() requires" in result.stderr


@pytest.mark.parametrize("backend", ["NATIVE", "NATIVE_CROSS", "PYTHON"])
def test_cmake_discovers_defer_and_tracks_headers(
    tmp_path: Path, compiler_environment: dict[str, str], backend: str
) -> None:
    cmake = shutil.which("cmake")
    ninja = shutil.which("ninja")
    clangxx = shutil.which("clang++")
    if cmake is None or ninja is None or CLANG is None or clangxx is None:
        pytest.skip("CMake, Ninja, and Clang are required")
    project = tmp_path / "project"
    source_dir = project / "src"
    tool_dir = source_dir / "tooling" / "defer"
    source_dir.mkdir(parents=True)
    tool_dir.mkdir(parents=True)
    shutil.copy2(TOOL, tool_dir / "defer.py")
    shutil.copy2(TOOL.parent / "scan.py", tool_dir / "scan.py")
    shutil.copy2(TOOL.parent / "plan.py", tool_dir / "plan.py")
    shutil.copy2(TOOL.parent / "pyproject.toml", tool_dir / "pyproject.toml")
    shutil.copy2(TOOL.parent / "uv.lock", tool_dir / "uv.lock")
    shutil.copytree(TOOL.parent / "vendor" / "clang", tool_dir / "vendor" / "clang")
    if backend != "PYTHON":
        for pattern in ("*.cpp", "*.h", "CMakeLists.txt"):
            for path in TOOL.parent.glob(pattern):
                shutil.copy2(path, tool_dir / path.name)
        dependencies = project / "cmake" / "deps"
        dependencies.mkdir(parents=True)
        shutil.copy2(ROOT / "cmake" / "deps" / "LLVMDevelopment.cmake", dependencies)
    second = source_dir / "second.c"
    second.write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n'
        "#ifndef SECOND_REQUIRED\n#error missing source definition\n#endif\n"
        "void second(void) { de\\\nfer /* cleanup (LIFO) */ ((void)0); }\n",
        encoding="utf-8",
    )
    (source_dir / "named.c").write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n'
        "void named(void) { defer_named(cleanup, (void)0); defer_cancel(cleanup); }\n",
        encoding="utf-8",
    )
    main = source_dir / "main.c"
    main.write_text("int main(void) { return 0; }\n", encoding="utf-8")
    header = source_dir / "value.h"
    header.write_text("#define VALUE 1\n", encoding="utf-8")
    (project / "ready.in").write_text("#define READY 1\n", encoding="utf-8")
    include_dir = (ROOT / "include").as_posix()
    module = (ROOT / "cmake" / "tooling" / "Defer.cmake").as_posix()
    (project / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.30)\n"
        "project(defer_test C CXX)\n"
        "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        'add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/ready.h" '
        'COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/ready.in" "${CMAKE_BINARY_DIR}/ready.h" '
        'DEPENDS "${CMAKE_SOURCE_DIR}/ready.in")\n'
        'add_custom_target(generate_version DEPENDS "${CMAKE_BINARY_DIR}/ready.h")\n'
        'set(ASCIICHAT_PYTHON3_EXECUTABLE "python-must-not-run")\n'
        f'include("{module}")\n'
        "add_subdirectory(src)\n"
        "ascii_defer_prepare()\n",
        encoding="utf-8",
    )
    (source_dir / "CMakeLists.txt").write_text(
        "add_executable(ascii-chat-probe main.c second.c named.c)\n"
        "set_source_files_properties(second.c PROPERTIES COMPILE_DEFINITIONS SECOND_REQUIRED)\n"
        "set_target_properties(ascii-chat-probe PROPERTIES UNITY_BUILD ON)\n"
        "add_dependencies(ascii-chat-probe generate_version)\n"
        f'target_include_directories(ascii-chat-probe PRIVATE "{include_dir}" "${{CMAKE_BINARY_DIR}}")\n',
        encoding="utf-8",
    )
    build = project / "out"

    def run(*args: str) -> str:
        result = subprocess.run(
            args, capture_output=True, text=True, env=compiler_environment, check=False
        )
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout

    configure = [
        cmake,
        "-S",
        str(project),
        "-B",
        str(build),
        "-G",
        "Ninja",
        f"-DCMAKE_C_COMPILER={CLANG}",
        f"-DCMAKE_CXX_COMPILER={clangxx}",
        f"-DASCIICHAT_DEFER_BACKEND={'PYTHON' if backend == 'PYTHON' else 'NATIVE'}",
        f"-DASCIICHAT_LIBCLANG_PATH={compiler_environment['LIBCLANG_LIBRARY_FILE']}",
    ]
    if backend == "NATIVE_CROSS":
        system = {"win32": "Windows", "darwin": "Darwin"}.get(sys.platform, "Linux")
        configure.append(f"-DCMAKE_SYSTEM_NAME={system}")
    run(*configure)
    if backend == "PYTHON":
        assert (build / "defer-python-venv" / "pyvenv.cfg").is_file()
    if backend != "PYTHON":
        native = (
            build
            / "defer-host"
            / (
                "defer-tool.exe"
                if sys.platform == "win32"
                else "defer-tool"
            )
        )
        assert not native.exists(), (
            "the native tool must be built during the build, not configuration"
        )
    run(cmake, "--build", str(build))
    main.write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n#include "value.h"\n#include "ready.h"\n'
        "static int n; static void f(void) { defer\n (n = VALUE); }\n"
        "int main(void) { f(); return n != VALUE; }\n",
        encoding="utf-8",
    )
    (build / "ready.h").unlink()
    output = run(cmake, "--build", str(build))
    assert output.count("Transforming defer source batch") == 1
    assert (
        build / "defer_transformed" / "ascii-chat-probe" / "src" / "second.c"
    ).is_file()
    generated = build / "defer_transformed" / "ascii-chat-probe" / "src" / "main.c"
    assert generated.is_file()
    assert "defer\n (n = VALUE)" in main.read_text(encoding="utf-8")
    assert "defer\n (n = VALUE)" not in generated.read_text(encoding="utf-8")
    executable = (
        build
        / "src"
        / ("ascii-chat-probe.exe" if sys.platform == "win32" else "ascii-chat-probe")
    )
    run(str(executable))
    other_generated = (
        build / "defer_transformed" / "ascii-chat-probe" / "src" / "second.c"
    )
    objects = list(
        (build / "src" / "CMakeFiles" / "ascii-chat-probe.dir").rglob(
            "*.obj" if sys.platform == "win32" else "*.o"
        )
    )
    other_object = next(path for path in objects if "second.c" in path.name)
    before = generated.stat().st_mtime_ns
    other_before = other_generated.stat().st_mtime_ns
    object_before = other_object.stat().st_mtime_ns
    # Some filesystems have coarse timestamp resolution.
    import time

    time.sleep(1.1)
    header.write_text("#define VALUE 2\n", encoding="utf-8")
    run(cmake, "--build", str(build))
    assert generated.stat().st_mtime_ns == before
    assert other_generated.stat().st_mtime_ns == other_before
    assert other_object.stat().st_mtime_ns == object_before
    run(str(executable))
    time.sleep(1.1)
    main.write_text(
        main.read_text(encoding="utf-8") + "\n/* changed */\n", encoding="utf-8"
    )
    run(cmake, "--build", str(build))
    assert generated.stat().st_mtime_ns > before
    assert other_generated.stat().st_mtime_ns == other_before
    assert other_object.stat().st_mtime_ns == object_before
    assert "Transforming defer source batch" not in run(cmake, "--build", str(build))


@pytest.mark.parametrize("alias_kind", ["same", "hardlink", "symlink"])
def test_source_cannot_be_overwritten(
    tmp_path: Path, transform_environment: dict[str, str], alias_kind: str
) -> None:
    source = tmp_path / "original.c"
    original = "#define defer(...) do {} while (0)\nvoid f(void) { defer((void)0); }\n"
    source.write_text(original, encoding="utf-8")
    output = source
    if alias_kind != "same":
        output = tmp_path / "alias.c"
        try:
            if alias_kind == "hardlink":
                os.link(source, output)
            else:
                output.symlink_to(source)
        except OSError as error:
            pytest.skip(f"{alias_kind} unavailable: {error}")
    result = subprocess.run(
        [
            *tool_command(transform_environment),
            str(source),
            str(output),
            "--",
            "-std=c17",
        ],
        capture_output=True,
        text=True,
        env=transform_environment,
        check=False,
    )
    assert result.returncode != 0
    assert source.read_text(encoding="utf-8") == original


def test_preserves_source_file_and_line_macros(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    original_path = (tmp_path / "input.c").as_posix()
    source = f'''static int in_cleanup, after_defer;
static const char *file_name;
static void f(void) {{
    defer({{
        in_cleanup = __LINE__;
        file_name = __FILE__;
    }});
    after_defer = __LINE__;
}}
static int equal(const char *a, const char *b) {{
    while (*a && *a == *b) {{ ++a; ++b; }}
    return *a == *b;
}}
int main(void) {{
    f();
    return !(in_cleanup == 6 && after_defer == 9 && equal(file_name, "{original_path}"));
}}
'''
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("lines.exe" if sys.platform == "win32" else "lines")
    compiled = subprocess.run(
        [str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, f"exit {run.returncode}: {run.stderr}"


def test_utf8_bom_is_preserved(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = tmp_path / "bom.c"
    source.write_bytes(
        b"\xef\xbb\xbf#define defer(...) do {} while (0)\nvoid f(void) { defer((void)0); }\n"
    )
    output = tmp_path / "generated.c"
    result = subprocess.run(
        [
            *tool_command(transform_environment),
            str(source),
            str(output),
            "--",
            "-std=c17",
        ],
        capture_output=True,
        text=True,
        env=transform_environment,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    assert output.read_bytes().startswith(b"\xef\xbb\xbf")
    compiled = subprocess.run(
        [str(CLANG), "-std=c17", "-fsyntax-only", str(output)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stderr


def test_return_conversion_happens_before_cleanup(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = """
#pragma STDC FENV_ACCESS ON
#include <fenv.h>
static float f(double value) { defer(fesetround(FE_UPWARD)); return value; }
int main(void) {
    volatile double value = 1.0 + 0x1p-24;
    if (fesetround(FE_DOWNWARD)) return 2;
    float result = f(value);
    return result != 1.0f;
}
"""
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("round.exe" if sys.platform == "win32" else "round")
    command = [
        str(CLANG),
        "-std=c17",
        "-frounding-math",
        str(tmp_path / "output.c"),
        "-o",
        str(executable),
    ]
    if sys.platform != "win32":
        command.append("-lm")
    compiled = subprocess.run(command, capture_output=True, text=True, check=False)
    assert compiled.returncode == 0, compiled.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, f"exit {run.returncode}: {run.stderr}"


def test_struct_and_function_pointer_return_types(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = """
static int cleaned;
struct item { int value; };
typedef int (*callback_t)(int);
static int plus_one(int value) { return value + 1; }
static struct item make_item(int value) {
    struct item result = {value};
    defer(cleaned++);
    return result;
}
static callback_t choose_callback(int ignored) {
    (void)ignored;
    defer(cleaned++);
    return plus_one;
}
int main(void) {
    struct item item = make_item(4);
    callback_t callback = choose_callback(0);
    return !(item.value == 4 && callback(4) == 5 && cleaned == 2);
}
"""
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("types.exe" if sys.platform == "win32" else "types")
    compiled = subprocess.run(
        [str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, f"exit {run.returncode}: {run.stderr}"


def test_compilation_database_separator_keeps_parser_flags_active(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    original = tmp_path / "original.c"
    generated = tmp_path / "generated.c"
    original.write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\nvoid f(void) { defer((void)0); }\n',
        encoding="utf-8",
    )
    database = [
        {
            "directory": str(tmp_path),
            "file": str(generated),
            "arguments": [
                str(CLANG),
                "-std=c17",
                "-I",
                str(ROOT / "include"),
                "-c",
                "--",
                str(generated),
            ],
        }
    ]
    (tmp_path / "compile_commands.json").write_text(
        json.dumps(database), encoding="utf-8"
    )
    driver = tmp_path / "driver"
    driver.mkdir()
    result = subprocess.run(
        [
            *tool_command(transform_environment),
            "--compilation-database",
            str(tmp_path),
            "--depfile",
            "deps.d",
            str(original),
            str(generated),
        ],
        capture_output=True,
        text=True,
        env=transform_environment,
        cwd=driver,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    assert generated.is_file()
    assert (driver / "deps.d").is_file()


def test_multiline_return_preserves_expression_line(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(
        tmp_path,
        transform_environment,
        "int f(void) { defer((void)0); return\n__LINE__; }\n"
        "int main(void) { return f() != 3; }\n",
    )
    assert result.returncode == 0, result.stderr
    executable = tmp_path / (
        "return-line.exe" if sys.platform == "win32" else "return-line"
    )
    compiled = subprocess.run(
        [str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stderr


@pytest.mark.parametrize(
    "source",
    [
        pytest.param(
            r"""
static int trace, releases;
static void release(int id) { trace = trace * 10 + id; releases++; }
static int use_resources(int fail) {
    defer(release(1));
    if (fail == 1) return 11;
    { defer(release(2)); if (fail == 2) return 22; }
    return 0;
}
int main(void) {
    if (use_resources(1) != 11 || trace != 1 || releases != 1) return 1;
    trace = releases = 0;
    if (use_resources(2) != 22 || trace != 21 || releases != 2) return 2;
    trace = releases = 0;
    if (use_resources(0) != 0 || trace != 21 || releases != 2) return 3;
    return 0;
}
""",
            id="resource-cleanup-early-return-and-fallthrough",
        ),
        pytest.param(
            r"""
static int trace;
static void record(int n) { trace = trace * 10 + n; }
static void work(int selected) {
    for (int i = 0; i < 3; i++) {
        defer(record(1));
        switch (selected) {
        case 0: { defer(record(2)); continue; }
        case 1: { defer(record(3)); break; }
        default: { defer(record(4)); goto done; }
        }
        break;
    }
done:;
}
int main(void) {
    work(0); if (trace != 212121) return 1;
    trace = 0; work(1); if (trace != 31) return 2;
    trace = 0; work(2); if (trace != 41) return 3;
    return 0;
}
""",
            id="nested-switch-loop-continue-break-goto",
        ),
        pytest.param(
            r"""
static int trace;
static void record(int n) { trace = trace * 10 + n; }
static int descend(int n) {
    defer(record(n));
    if (n == 0) return 7;
    return descend(n - 1) + 1;
}
int main(void) {
    int value = descend(3);
    return value != 10 || trace != 123;
}
""",
            id="recursive-calls-own-cleanup-state",
        ),
        pytest.param(
            r"""
static int values[2] = {1, 2};
static int *choose(int which) {
    int *result = &values[which];
    defer(result = &values[1 - which]);
    return result;
}
static int *cleanup_side_effect(int which) {
    defer(values[0] = 9);
    return &values[which];
}
int main(void) {
    if (choose(0) != &values[0]) return 1;
    if (cleanup_side_effect(1) != &values[1] || values[0] != 9) return 2;
    return 0;
}
""",
            id="pointer-return-captured-before-cleanup",
        ),
        pytest.param(
            r"""
static int trace;
static void record(int n) { trace = trace * 10 + n; }
static void work(int skip) {
    if (skip) goto end;
    defer(record(1));
    { defer(record(2)); goto end; }
end:;
}
int main(void) {
    work(1); if (trace != 0) return 1;
    work(0); if (trace != 21) return 2;
    return 0;
}
""",
            id="forward-goto-registration-and-inner-cleanup",
        ),
    ],
)
def test_varied_c_programs(
    tmp_path: Path, transform_environment: dict[str, str], source: str
) -> None:
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    compiled = subprocess.run(
        [str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stderr
    run = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert run.returncode == 0, f"exit {run.returncode}: {run.stderr}"
