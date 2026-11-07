"""Compile and execute transformed C to check defer's control-flow contract."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "src" / "tooling" / "defer" / "defer.py"
CLANG = shutil.which("clang")


@pytest.fixture
def compiler_environment() -> dict[str, str]:
    if CLANG is None:
        pytest.skip("Clang is not installed")
    env = os.environ.copy()
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


def transform(
    tmp_path: Path, env: dict[str, str], source: str
) -> subprocess.CompletedProcess[str]:
    original = tmp_path / "input.c"
    original.write_text(
        "#define defer(...) do {} while (0)\n" + source, encoding="utf-8"
    )
    return subprocess.run(
        [
            sys.executable,
            str(TOOL),
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
    tmp_path: Path, compiler_environment: dict[str, str]
) -> None:
    result = transform(
        tmp_path,
        compiler_environment,
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
    { defer(record(20)); defer({ int a = 20, b = 1; record(a + b); }); }
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
    compiler_environment: dict[str, str],
    source: str,
) -> None:
    result = transform(tmp_path, compiler_environment, source)
    assert result.returncode != 0
    assert not (tmp_path / "output.c").exists()
    assert "defer:" in result.stderr


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


def test_cmake_discovers_defer_and_tracks_headers(
    tmp_path: Path, compiler_environment: dict[str, str]
) -> None:
    cmake = shutil.which("cmake")
    ninja = shutil.which("ninja")
    if cmake is None or ninja is None or CLANG is None:
        pytest.skip("CMake, Ninja, and Clang are required")
    project = tmp_path / "project"
    source_dir = project / "src"
    tool_dir = source_dir / "tooling" / "defer"
    source_dir.mkdir(parents=True)
    tool_dir.mkdir(parents=True)
    shutil.copy2(TOOL, tool_dir / "defer.py")
    shutil.copytree(TOOL.parent / "vendor" / "clang", tool_dir / "vendor" / "clang")
    second = source_dir / "second.c"
    second.write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n'
        "void second(void) { defer((void)0); }\n",
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
        "project(defer_test C)\n"
        "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        'add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/ready.h" '
        'COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/ready.in" "${CMAKE_BINARY_DIR}/ready.h" '
        'DEPENDS "${CMAKE_SOURCE_DIR}/ready.in")\n'
        'add_custom_target(generate_version DEPENDS "${CMAKE_BINARY_DIR}/ready.h")\n'
        f'set(ASCIICHAT_PYTHON3_EXECUTABLE "{Path(sys.executable).as_posix()}")\n'
        f'include("{module}")\n'
        "add_subdirectory(src)\n"
        "ascii_defer_prepare()\n",
        encoding="utf-8",
    )
    (source_dir / "CMakeLists.txt").write_text(
        "add_executable(ascii-chat-probe main.c second.c)\n"
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

    run(
        cmake,
        "-S",
        str(project),
        "-B",
        str(build),
        "-G",
        "Ninja",
        f"-DCMAKE_C_COMPILER={CLANG}",
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


def test_source_cannot_be_overwritten(
    tmp_path: Path, compiler_environment: dict[str, str]
) -> None:
    source = tmp_path / "original.c"
    original = "#define defer(...) do {} while (0)\nvoid f(void) { defer((void)0); }\n"
    source.write_text(original, encoding="utf-8")
    result = subprocess.run(
        [sys.executable, str(TOOL), str(source), str(source), "--", "-std=c17"],
        capture_output=True,
        text=True,
        env=compiler_environment,
        check=False,
    )
    assert result.returncode != 0
    assert source.read_text(encoding="utf-8") == original


def test_preserves_source_file_and_line_macros(
    tmp_path: Path, compiler_environment: dict[str, str]
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
    result = transform(tmp_path, compiler_environment, source)
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
    tmp_path: Path, compiler_environment: dict[str, str]
) -> None:
    source = tmp_path / "bom.c"
    source.write_bytes(
        b"\xef\xbb\xbf#define defer(...) do {} while (0)\nvoid f(void) { defer((void)0); }\n"
    )
    output = tmp_path / "generated.c"
    result = subprocess.run(
        [sys.executable, str(TOOL), str(source), str(output), "--", "-std=c17"],
        capture_output=True,
        text=True,
        env=compiler_environment,
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
    tmp_path: Path, compiler_environment: dict[str, str]
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
    result = transform(tmp_path, compiler_environment, source)
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
    tmp_path: Path, compiler_environment: dict[str, str]
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
    result = transform(tmp_path, compiler_environment, source)
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
    tmp_path: Path, compiler_environment: dict[str, str]
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
            sys.executable,
            str(TOOL),
            "--compilation-database",
            str(tmp_path),
            "--depfile",
            "deps.d",
            str(original),
            str(generated),
        ],
        capture_output=True,
        text=True,
        env=compiler_environment,
        cwd=driver,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    assert generated.is_file()
    assert (driver / "deps.d").is_file()
