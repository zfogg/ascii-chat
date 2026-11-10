"""Compile and execute transformed C to check defer's control-flow contract."""

from __future__ import annotations

import json
import importlib.util
import os
import random
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time

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


def test_python_manifest_parse_returns_records() -> None:
    spec = importlib.util.spec_from_file_location("ascii_defer_manifest_test", TOOL.parent / "manifest.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
        manifest = module.Manifest.parse('a.c\tout/a.c\r\nb.c\tout/b.c\t["-I", "dir with spaces"]\n')
        assert len(manifest.records) == 2
        assert manifest.records[0].source == Path("a.c")
        assert manifest.records[0].arguments == []
        assert manifest.records[1].output == Path("out/b.c")
        assert manifest.records[1].arguments == ["-I", "dir with spaces"]
        assert module.Manifest.parse('a.c\tout.c\t["x\u2028y"]\n').records[0].arguments == ["x\u2028y"]
        with pytest.raises(module.ManifestError, match="cannot be combined"):
            module.Manifest.parse('a.c\tout.c\t[]\n', compilation_database=True)
    finally:
        del sys.modules[spec.name]


def test_python_batch_plan_uses_explicit_tool_source_root(tmp_path: Path) -> None:
    cmake = shutil.which("cmake")
    if not cmake or not shutil.which("ninja") or not CLANG:
        pytest.skip("CMake, Ninja, and Clang are required")
    project = tmp_path / "nested project"
    project.mkdir()
    source = project / "source.c"
    source.write_text('void f(void) {}\n', encoding="utf-8")
    build = tmp_path / "build"
    records = tmp_path / "records.txt"
    records.write_text(f"ascii-chat-probe\t{source}\t{build / 'generated.c'}\t\n", encoding="utf-8")
    plan = tmp_path / "plan.cmake"
    result = subprocess.run(
        [sys.executable, str(TOOL.parent / "plan.py"), "--records", str(records), "--build-dir", str(build),
         "--stage", "test", "--output", str(plan)], capture_output=True, text=True, check=False,
    )
    assert result.returncode == 0, result.stderr
    (project / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(plan_test C)\n'
        f'set(ASCIICHAT_DEFER_SOURCE_ROOT "{ROOT.as_posix()}")\n'
        'set(ASCIICHAT_DEFER_PYTHON_COMMAND "${CMAKE_COMMAND};-E;echo")\n'
        'set(ASCIICHAT_LIBCLANG_PATH dummy)\nadd_library(ascii-chat-probe STATIC source.c)\n'
        f'include("{plan.as_posix()}")\n', encoding="utf-8"
    )
    result = subprocess.run([cmake, "-S", str(project), "-B", str(build), "-G", "Ninja", f"-DCMAKE_C_COMPILER={CLANG}"],
                            capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    rules = (build / "build.ninja").read_text(encoding="utf-8").replace("\\", "/").replace("$:", ":").replace("$ ", " ")
    assert f"{ROOT.as_posix()}/src/tooling/defer/defer.py" in rules
    assert f"{ROOT.as_posix()}/src/tooling/defer/analysis.py" in rules
    assert "--batch" in rules
    assert "--stdin" not in rules


@pytest.mark.parametrize("form", ["automatic", "dash"])
def test_stdin_stdout_transforms_and_executes(
    tmp_path: Path, transform_environment: dict[str, str], form: str
) -> None:
    virtual = tmp_path / "virtual" / "source.c"
    virtual.parent.mkdir()
    (virtual.parent / "value.h").write_text('#define VALUE 7\n', encoding="utf-8")
    source = (b'#include "value.h"\n#include <ascii-chat/tooling/defer/defer.h>\n'
              b'int main(void) { int n=0; { defer(n=VALUE); } return n!=7; }\n')
    command = [*tool_command(transform_environment), "--filename", str(virtual)]
    command += [] if form == "automatic" else ["-", "-"]
    result = subprocess.run([*command, "--", "-std=c17", "-I", str(ROOT / "include")],
                            input=source, capture_output=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert b"__ascii_defer_active_" in result.stdout
    assert not virtual.exists()
    output = tmp_path / "generated.c"
    output.write_bytes(result.stdout)
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([str(CLANG), "-std=c17", "-I", str(ROOT / "include"), "-I", str(virtual.parent),
                             str(output), "-o", str(executable)], capture_output=True, text=True,
                            env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


@pytest.mark.parametrize("form", ["automatic", "dash"])
def test_stdin_rejects_invalid_cleanup_without_output(
    tmp_path: Path, transform_environment: dict[str, str], form: str
) -> None:
    command = [*tool_command(transform_environment), "--filename", str(tmp_path / "input.c")]
    command += [] if form == "automatic" else ["-", "-"]
    result = subprocess.run([*command, "--", "-std=c17", "-I", str(ROOT / "include")],
                            input=b'#include <ascii-chat/tooling/defer/defer.h>\nvoid f(void) { defer(missing()); }\n',
                            capture_output=True, env=transform_environment, check=False)
    assert result.returncode != 0
    assert result.stdout == b""
    assert b"missing" in result.stderr


def test_stdin_uses_original_source_compilation_database(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source = tmp_path / "virtual.c"
    commands = [{"directory": str(tmp_path), "file": str(source), "arguments": [
        CLANG, "-std=c17", "-DVALUE=7", "-c", str(source), "-o", str(tmp_path / "source.o")
    ]}]
    database = tmp_path / "compile_commands.json"
    database.write_text(json.dumps(commands), encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), "--filename", str(source), "--compilation-database", str(database)],
        input=b'#if VALUE != 7\n#error missing flags\n#endif\nvoid f(void) {}\n', capture_output=True,
        env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert b"void f(void)" in result.stdout
    assert not source.exists()


@pytest.mark.parametrize("option", ["--depfile", "--header-overlay"])
def test_stdin_rejects_output_options(tmp_path: Path, transform_environment: dict[str, str], option: str) -> None:
    result = subprocess.run([*tool_command(transform_environment), option, str(tmp_path / "out.d")],
                            input=b'void f(void) {}\n', capture_output=True, env=transform_environment, check=False)
    assert result.returncode != 0
    assert result.stdout == b""
    assert not (tmp_path / "out.d").exists()


def test_batch_ignores_redirected_stdin(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source, output = tmp_path / "input.c", tmp_path / "output.c"
    source.write_text("void file_input(void) {}\n", encoding="utf-8")
    manifest = tmp_path / "batch.txt"
    manifest.write_text(f"{source}\t{output}\n", encoding="utf-8")
    result = subprocess.run([*tool_command(transform_environment), "--batch", str(manifest)],
                            input=b"invalid piped input!\n", capture_output=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert result.stdout == b""
    assert "file_input" in output.read_text(encoding="utf-8")


def test_file_mode_ignores_redirected_stdin(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source, output = tmp_path / "input.c", tmp_path / "output.c"
    source.write_text("void file_input(void) {}\n", encoding="utf-8")
    result = subprocess.run([*tool_command(transform_environment), str(source), str(output), "--", "-std=c17"],
                            input=b"invalid piped input!\n", capture_output=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert result.stdout == b""
    assert "file_input" in output.read_text(encoding="utf-8")


def test_automatic_stdin_uses_default_filename(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    result = subprocess.run([*tool_command(transform_environment), "--", "-std=c17"],
                            input=b'int value;\n', capture_output=True, env=transform_environment,
                            cwd=tmp_path, check=False)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert b"stdin.c" in result.stdout
    assert b"int value;" in result.stdout


def test_stdin_flag_is_not_advertised(transform_environment: dict[str, str]) -> None:
    result = subprocess.run([*tool_command(transform_environment), "--help"], capture_output=True,
                            text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert "--stdin" not in result.stdout
    assert "redirected stdin" in result.stdout


@pytest.mark.skipif(sys.platform == "win32", reason="requires a POSIX pseudo-terminal")
def test_terminal_without_input_files_does_not_wait_for_stdin(transform_environment: dict[str, str]) -> None:
    import pty

    master, slave = pty.openpty()
    try:
        result = subprocess.run([*tool_command(transform_environment), "--", "-std=c17"], stdin=slave,
                                capture_output=True, env=transform_environment, timeout=10, check=False)
    finally:
        os.close(master)
        os.close(slave)
    assert result.returncode != 0
    assert result.stdout == b""


@pytest.mark.parametrize("extension", ["cpp", "hpp", "m"])
def test_stdin_supports_cpp_and_objc_languages(
    tmp_path: Path, transform_environment: dict[str, str], extension: str
) -> None:
    virtual = tmp_path / f"input.{extension}"
    flags = ["-I", str(ROOT / "include")]
    if extension == "m":
        flags += ["-fobjc-exceptions", *objc_syntax_runtime()]
        source = b'#include <ascii-chat/tooling/defer/defer.h>\nvoid f(void) { defer((void)0); }\n'
    else:
        flags += ["-x", "c++", "-std=c++11"]
        source = b'#include <ascii-chat/tooling/defer/defer.h>\nvoid f() { defer((void)0); }\n'
    result = subprocess.run([*tool_command(transform_environment), "--filename", str(virtual), "--", *flags],
                            input=source, capture_output=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    if extension == "m":
        assert b"@finally" in result.stdout
    else:
        assert b"defer((void)0)" in result.stdout
    output = tmp_path / f"output.{extension}"
    output.write_bytes(result.stdout)
    result = subprocess.run([str(CLANG), *flags, "-fsyntax-only", str(output)], capture_output=True, text=True,
                            env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr


def test_stdin_uses_piped_contents_instead_of_virtual_file(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    virtual = tmp_path / "input.c"
    virtual.write_text("int disk_only;\n", encoding="utf-8")
    result = subprocess.run([*tool_command(transform_environment), "--filename", str(virtual)],
                            input=b'int pipe_only;\n', capture_output=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert b"pipe_only" in result.stdout and b"disk_only" not in result.stdout
    assert virtual.read_text(encoding="utf-8") == "int disk_only;\n"


@pytest.mark.parametrize("named", [False, True])
def test_stdin_accepts_redirected_binary_files(tmp_path: Path, transform_environment: dict[str, str], named: bool) -> None:
    source, output = tmp_path / "input.c", tmp_path / "output.c"
    source.write_bytes(b'\xef\xbb\xbf#define defer(...) do {} while (0)\r\nvoid f(void) { defer((void)0); }\r\n')
    with source.open("rb") as input_file, output.open("wb") as output_file:
        result = subprocess.run([*tool_command(transform_environment), *(["--filename", str(source)] if named else [])],
                                stdin=input_file, stdout=output_file, stderr=subprocess.PIPE,
                                env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert output.read_bytes().startswith(b"\xef\xbb\xbf")
    assert b"__ascii_defer_active_" in output.read_bytes()


@pytest.mark.parametrize("extension", ["c", "cpp", "m"])
def test_stdin_matches_file_transformation(
    tmp_path: Path, transform_environment: dict[str, str], extension: str
) -> None:
    source, output = tmp_path / f"input.{extension}", tmp_path / f"output.{extension}"
    content = b'#include <ascii-chat/tooling/defer/defer.h>\nvoid f(void) { defer((void)0); }\n'
    source.write_bytes(content)
    flags = ["-I", str(ROOT / "include")]
    if extension == "m":
        flags += ["-fobjc-exceptions", *objc_syntax_runtime()]
    elif extension == "cpp":
        flags += ["-std=c++11"]
    file_result = subprocess.run([*tool_command(transform_environment), str(source), str(output), "--", *flags],
                                 capture_output=True, env=transform_environment, check=False)
    assert file_result.returncode == 0, file_result.stderr.decode(errors="replace")
    stream_result = subprocess.run([*tool_command(transform_environment), "--filename", str(source), "--", *flags],
                                   input=content, capture_output=True, env=transform_environment, check=False)
    assert stream_result.returncode == 0, stream_result.stderr.decode(errors="replace")
    assert stream_result.stdout == output.read_bytes()


def test_native_stdin_with_preamble(tmp_path: Path, compiler_environment: dict[str, str]) -> None:
    binary = compiler_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not binary:
        pytest.skip("native defer binary is required")
    command = [binary, "--filename", str(tmp_path / "virtual.c"), "--", "-std=c17"]
    source = b'#define defer(...) do {} while (0)\nvoid f(void) { defer((void)0); }\n'
    ordinary = subprocess.run(command, input=source, capture_output=True, env=compiler_environment, check=False)
    reused = subprocess.run([binary, "--preamble", *command[1:]], input=source, capture_output=True,
                            env=compiler_environment, check=False)
    assert ordinary.returncode == 0, ordinary.stderr.decode(errors="replace")
    assert reused.returncode == 0, reused.stderr.decode(errors="replace")
    assert reused.stdout == ordinary.stdout


def objc_syntax_runtime() -> list[str]:
    return [] if sys.platform == "darwin" else ["-fobjc-runtime=gnustep-2.0"]


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


def test_explain_understands_wrapped_defer(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = tmp_path / "input.c"
    source.write_text(
        '#define defer(...) do {} while (0)\n#define INNER(x) defer(x++)\n#define OUTER(x) INNER(x)\n'
        'void f(int n) {\n  OUTER(n);\n  return;\n}\n', encoding="utf-8",
    )
    result = subprocess.run(
        [*tool_command(transform_environment), "--explain", f"{source}:6", "--", "-std=c17"],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "n ++" in result.stdout
    assert "registered at line 5" in result.stdout


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


def test_incremental_cache_cannot_replace_source(tmp_path: Path, base_environment: dict[str, str]) -> None:
    binary = base_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not binary:
        pytest.skip("native defer binary was not supplied")
    source = tmp_path / "generated.cache"
    source.write_text("void f(void) {}\n", encoding="utf-8")
    output = tmp_path / "generated" / "output.c"
    manifest = tmp_path / "batch.txt"
    manifest.write_text(f"{source}\t{output}\n", encoding="utf-8")
    result = subprocess.run(
        [binary, "--batch", str(manifest), "--compilation-database", str(tmp_path), "--dependency-stamps", "--incremental"],
        capture_output=True, text=True, env=base_environment, check=False,
    )
    assert result.returncode != 0
    assert "incremental cache must not overwrite" in result.stderr
    assert source.read_text(encoding="utf-8") == "void f(void) {}\n"


@pytest.mark.parametrize("extension", ["c", "cpp", "m", "mm"])
def test_shared_cache_reuses_across_build_directories(
    tmp_path: Path, compiler_environment: dict[str, str], extension: str
) -> None:
    binary = compiler_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not binary or CLANG is None:
        pytest.skip("native defer binary and Clang are required")
    source = tmp_path / f"source.{extension}"
    source.write_text(
        '#include "generated.h"\n#include <ascii-chat/tooling/defer/defer.h>\n'
        'int value; void f(void) { defer(value = VALUE); }\n', encoding="utf-8"
    )
    shared = tmp_path / "shared"
    early = tmp_path / "early"
    early.mkdir()

    def prepare(name: str, value: int) -> tuple[Path, Path, Path]:
        build = tmp_path / name
        build.mkdir()
        (build / "generated.h").write_text(f"#define VALUE {value}\n", encoding="utf-8")
        output = build / "generated" / f"source.{extension}"
        manifest = build / "batch.txt"
        manifest.write_text(f"{source}\t{output}\n", encoding="utf-8")
        commands = [{"directory": str(tmp_path), "file": str(output), "arguments": [
            CLANG, *(["-fobjc-exceptions", *objc_syntax_runtime()] if extension in ("m", "mm") else []),
            "-I", str(early), "-I", str(ROOT / "include"), "-I", str(build), "-c", str(output), "-o", str(build / "source.o")
        ]}]
        (build / "compile_commands.json").write_text(json.dumps(commands), encoding="utf-8")
        return build, output, manifest

    def run(build: Path, manifest: Path) -> str:
        result = subprocess.run(
            [binary, "--batch", str(manifest), "--compilation-database", str(build), "--dependency-stamps",
             "--incremental", "--shared-cache", str(shared)],
            capture_output=True, text=True, env=compiler_environment, check=False,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stderr

    first, first_output, first_manifest = prepare("first", 1)
    second, second_output, second_manifest = prepare("second", 1)
    assert "transformed 1 of 1" in run(first, first_manifest)
    assert "transformed 0 of 1 files (1 restored" in run(second, second_manifest)
    assert first_output.read_text(encoding="utf-8") == second_output.read_text(encoding="utf-8")
    (early / "generated.h").write_text("#define VALUE 3\n", encoding="utf-8")
    shadowed, _, shadowed_manifest = prepare("shadowed", 1)
    assert "transformed 1 of 1" in run(shadowed, shadowed_manifest)
    (early / "generated.h").unlink()
    cache_data = next(path for path in shared.glob("*.data") if path.read_text(encoding="utf-8") == first_output.read_text(encoding="utf-8"))
    cache_data.unlink()
    os.link(source, cache_data)
    original_source = source.read_text(encoding="utf-8")
    hardlinked, _, hardlinked_manifest = prepare("hardlinked", 1)
    assert "transformed 1 of 1" in run(hardlinked, hardlinked_manifest)
    assert source.read_text(encoding="utf-8") == original_source
    cache_data.unlink()
    third, _, third_manifest = prepare("third", 2)
    assert "transformed 1 of 1" in run(third, third_manifest)
    database = second / "compile_commands.json"
    commands = json.loads(database.read_text(encoding="utf-8"))
    commands[0]["arguments"].insert(1, "-DCHANGED_FLAG=1")
    database.write_text(json.dumps(commands), encoding="utf-8")
    assert "transformed 1 of 1" in run(second, second_manifest)
    for cache_file in shared.glob("*.data"):
        cache_file.write_text("corrupt", encoding="utf-8")
    fourth, _, fourth_manifest = prepare("fourth", 1)
    assert "transformed 1 of 1" in run(fourth, fourth_manifest)
    source.write_text(source.read_text(encoding="utf-8") + "/* __TIME__ */\n", encoding="utf-8")
    volatile_first, _, volatile_first_manifest = prepare("volatile-first", 1)
    volatile_second, _, volatile_second_manifest = prepare("volatile-second", 1)
    assert "transformed 1 of 1" in run(volatile_first, volatile_first_manifest)
    assert "transformed 1 of 1" in run(volatile_second, volatile_second_manifest)
    source.write_text(source.read_text(encoding="utf-8").replace("__TIME__", "__has_include"), encoding="utf-8")
    probe_first, _, probe_first_manifest = prepare("probe-first", 1)
    probe_second, _, probe_second_manifest = prepare("probe-second", 1)
    assert "transformed 1 of 1" in run(probe_first, probe_first_manifest)
    assert "transformed 1 of 1" in run(probe_second, probe_second_manifest)


def test_preamble_mode_matches_and_rejects_invalid_code(tmp_path: Path, compiler_environment: dict[str, str]) -> None:
    binary = compiler_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not binary:
        pytest.skip("native defer binary was not supplied")
    source = tmp_path / "source.c"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'int value; void f(void) { defer(value = 1); }\n', encoding="utf-8"
    )
    outputs = [tmp_path / "default.c", tmp_path / "preamble.c"]
    for output, extra in zip(outputs, [[], ["--preamble"]], strict=True):
        result = subprocess.run(
            [binary, *extra, str(source), str(output), "--", "-I", str(ROOT / "include")],
            capture_output=True, text=True, env=compiler_environment, check=False,
        )
        assert result.returncode == 0, result.stderr
    assert outputs[0].read_text(encoding="utf-8") == outputs[1].read_text(encoding="utf-8")
    source.write_text('void f(void) { defer(return); }\n', encoding="utf-8")
    result = subprocess.run(
        [binary, "--preamble", str(source), str(tmp_path / "invalid.c"), "--", "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=compiler_environment, check=False,
    )
    assert result.returncode != 0
    assert not (tmp_path / "invalid.c").exists()


def test_cmake_shared_cache_across_build_directories(tmp_path: Path, compiler_environment: dict[str, str]) -> None:
    binary = compiler_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    cmake = shutil.which("cmake")
    clangxx = shutil.which("clang++")
    if not binary or not cmake or not clangxx or not CLANG or not shutil.which("ninja"):
        pytest.skip("native defer binary, CMake, Ninja, and Clang are required")
    source_dir = tmp_path / "src"
    source_dir.mkdir()
    (source_dir / "main.c").write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'int cpp_test(void); int main(void) { int result = 0; { defer(result = 1); } '
        'return result != 1 || !cpp_test(); }\n', encoding="utf-8"
    )
    (source_dir / "other.cpp").write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'extern "C" int cpp_test() { int value = 0; { defer(value = 1); } return value == 1; }\n',
        encoding="utf-8",
    )
    module = (ROOT / "cmake" / "tooling" / "Defer.cmake").as_posix()
    (tmp_path / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.30)\nproject(defer_cache C CXX)\n'
        'set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        f'include("{module}")\nadd_subdirectory(src)\nascii_defer_prepare()\n', encoding="utf-8"
    )
    (source_dir / "CMakeLists.txt").write_text(
        'add_executable(ascii-chat-probe main.c other.cpp)\n'
        f'target_include_directories(ascii-chat-probe PRIVATE "{(ROOT / "include").as_posix()}")\n',
        encoding="utf-8",
    )
    for name, expected in [("one", "transformed 1 of 1"), ("two", "transformed 0 of 1 files (1 restored")]:
        build = tmp_path / name
        selected_binary = compiler_environment.get("ASCIICHAT_DEFER_SECOND_BINARY", binary) if name == "two" else binary
        configured = subprocess.run(
            [cmake, "-S", str(tmp_path), "-B", str(build), "-G", "Ninja", f"-DCMAKE_C_COMPILER={CLANG}",
             f"-DCMAKE_CXX_COMPILER={clangxx}", f"-DASCIICHAT_DEFER_NATIVE_BINARY={selected_binary}",
             f"-DASCIICHAT_DEFER_SHARED_CACHE={tmp_path / 'shared'}"],
            capture_output=True, text=True, env=compiler_environment, check=False,
        )
        assert configured.returncode == 0, configured.stdout + configured.stderr
        built = subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True,
                               env=compiler_environment, check=False)
        assert built.returncode == 0, built.stdout + built.stderr
        assert expected in built.stdout + built.stderr
        exe = build / "src" / ("ascii-chat-probe.exe" if sys.platform == "win32" else "ascii-chat-probe")
        assert subprocess.run([str(exe)], check=False).returncode == 0


def test_incremental_cache_tracks_each_compile_command(tmp_path: Path, compiler_environment: dict[str, str]) -> None:
    binary = compiler_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not binary or not CLANG:
        pytest.skip("native defer binary and Clang are required")
    commands = []
    records = []
    for name in ("first", "second"):
        source = tmp_path / f"{name}.c"
        output = tmp_path / "generated" / name / f"{name}.c"
        source.write_text(
            '#include <ascii-chat/tooling/defer/defer.h>\n'
            f'void {name}(void) {{ defer((void)0); }}\n', encoding="utf-8"
        )
        commands.append({"directory": str(tmp_path), "file": str(output), "arguments": [
            CLANG, "-I", str(ROOT / "include"), "-c", str(output), "-o", str(tmp_path / f"{name}.o")
        ]})
        records.append(f"{source}\t{output}\n")
    database = tmp_path / "compile_commands.json"
    manifest = tmp_path / "batch.txt"
    manifest.write_text("".join(records), encoding="utf-8")

    def run() -> str:
        database.write_text(json.dumps(commands), encoding="utf-8")
        result = subprocess.run(
            [binary, "--batch", str(manifest), "--compilation-database", str(tmp_path), "--dependency-stamps",
             "--incremental"], capture_output=True, text=True, env=compiler_environment, check=False
        )
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stderr

    assert "transformed 2 of 2" in run()
    assert "transformed 0 of 2" in run()
    arguments = commands[0]["arguments"]
    assert isinstance(arguments, list)
    arguments.insert(1, "-DONLY_FIRST=1")
    assert "transformed 1 of 2" in run()


@pytest.mark.parametrize("mode", ["per_file", "database_directory", "database_file", "database_command"])
def test_batch_accepts_per_file_arguments_or_original_source_database(
    tmp_path: Path, transform_environment: dict[str, str], mode: str
) -> None:
    root = tmp_path / "project with spaces"
    root.mkdir()
    include_dir = root / "include with spaces"
    include_dir.mkdir()
    (include_dir / "definitions.h").write_text('#define OFFSET 0\n', encoding="utf-8")
    manifest = root / "batch.txt"
    records, commands, outputs = [], [], []
    for index, value in ((1, 11), (2, 22)):
        source = root / f"source{index}.c"
        output = root / f"generated{index}" / f"source{index}.c"
        source.write_text('#include "definitions.h"\n#define defer(...) do {} while (0)\n'
                          f'int f{index}(void) {{ int n=0; {{ defer(n=VALUE+OFFSET); }} return n; }}\n', encoding="utf-8")
        flags = ["-std=c17", f"-DVALUE={value}", "-I", str(include_dir)]
        outputs.append((output, flags))
        records.append(f"{source}\t{output}" + ("\t" + json.dumps(flags) if mode == "per_file" else "") + "\n")
        argv = [str(CLANG), *flags, "-c", source.name, "-o", f"source{index}.o"]
        entry: dict[str, str | list[str]] = {"directory": str(root), "file": str(source)}
        if mode == "database_command":
            entry["command"] = subprocess.list2cmdline(argv) if sys.platform == "win32" else shlex.join(argv)
        else:
            entry["arguments"] = argv
        commands.append(entry)
    manifest.write_text("".join(records), encoding="utf-8")
    command = [*tool_command(transform_environment), f"--batch={manifest}"] if mode == "per_file" else [
        *tool_command(transform_environment), "--batch", str(manifest)
    ]
    if mode != "per_file":
        database = root / "compile_commands.json"
        database.write_text(json.dumps(commands), encoding="utf-8")
        command += ["--compilation-database", str(database if mode == "database_file" else root)]
    result = subprocess.run(command, capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    objects = []
    for index, (output, flags) in enumerate(outputs):
        obj = root / f"object{index}.o"
        result = subprocess.run([str(CLANG), *flags, "-c", str(output), "-o", str(obj)],
                                capture_output=True, text=True, env=transform_environment, check=False)
        assert result.returncode == 0, result.stderr
        objects.append(str(obj))
    main = root / "main.c"
    main.write_text('int f1(void); int f2(void); int main(void) { return f1()!=11 || f2()!=22; }\n', encoding="utf-8")
    executable = root / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([str(CLANG), str(main), *objects, "-o", str(executable)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


@pytest.mark.parametrize("payload", ['{}', '[1]', '["bad\\u0000arg"]', 'not-json'])
def test_batch_rejects_invalid_per_file_arguments(
    tmp_path: Path, transform_environment: dict[str, str], payload: str
) -> None:
    source, output = tmp_path / "input.c", tmp_path / "output.c"
    source.write_text('void f(void) {}\n', encoding="utf-8")
    manifest = tmp_path / "batch.txt"
    manifest.write_text(f"{source}\t{output}\t{payload}\n", encoding="utf-8")
    result = subprocess.run([*tool_command(transform_environment), "--batch", str(manifest)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode != 0
    assert "JSON array of strings" in result.stderr
    assert not output.exists()


def test_compilation_database_prefers_generated_entry(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source, output = tmp_path / "input.c", tmp_path / "output.c"
    source.write_text('#if VALUE != 2\n#error wrong command\n#endif\nvoid f(void) {}\n', encoding="utf-8")
    commands = [
        {"directory": str(tmp_path), "file": str(path), "arguments": [
            CLANG, f"-DVALUE={value}", "-c", str(path), "-o", str(tmp_path / f"{value}.o")
        ]} for path, value in ((source, 1), (output, 2))
    ]
    (tmp_path / "compile_commands.json").write_text(json.dumps(commands), encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), "--compilation-database", str(tmp_path), str(source), str(output)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert output.exists()


def test_compilation_database_rejects_ambiguous_source_entry(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source, output = tmp_path / "input.c", tmp_path / "output.c"
    source.write_text('void f(void) {}\n', encoding="utf-8")
    commands = [{"directory": str(tmp_path), "file": str(source), "arguments": [
        CLANG, f"-DVALUE={value}", "-c", str(source), "-o", str(tmp_path / f"{value}.o")
    ]} for value in (1, 2)]
    (tmp_path / "compile_commands.json").write_text(json.dumps(commands), encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), "--compilation-database", str(tmp_path), str(source), str(output)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert "expected one compile command" in result.stderr
    assert not output.exists()


def test_compilation_database_removes_symlinked_input_argument(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source, alias, output = tmp_path / "source.c", tmp_path / "alias.c", tmp_path / "output.c"
    source.write_text('void f(void) {}\n', encoding="utf-8")
    try:
        alias.symlink_to(source)
    except OSError:
        pytest.skip("file symlinks are not available")
    commands = [{"directory": str(tmp_path), "file": str(source), "arguments": [
        CLANG, "-c", str(alias), "-o", str(tmp_path / "source.o")
    ]}]
    (tmp_path / "compile_commands.json").write_text(json.dumps(commands), encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), "--compilation-database", str(tmp_path), str(source), str(output)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert output.is_file()


@pytest.mark.parametrize("batch", [False, True])
def test_rejects_explicit_arguments_combined_with_database(
    tmp_path: Path, transform_environment: dict[str, str], batch: bool
) -> None:
    source, output = tmp_path / "input.c", tmp_path / "output.c"
    source.write_text('void f(void) {}\n', encoding="utf-8")
    manifest = tmp_path / "batch.txt"
    manifest.write_text(f'{source}\t{output}\t["-std=c17"]\n', encoding="utf-8")
    command = (["--batch", str(manifest)] if batch else [str(source), str(output), "--", "-std=c17"])
    result = subprocess.run(
        [*tool_command(transform_environment), "--compilation-database", str(tmp_path), *command],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert "cannot be combined with --compilation-database" in result.stderr
    assert not output.exists()


def test_incremental_batch_tracks_per_file_arguments(tmp_path: Path, compiler_environment: dict[str, str]) -> None:
    binary = compiler_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not binary:
        pytest.skip("native defer binary is required")
    manifest = tmp_path / "batch.txt"
    records = []
    for index in (1, 2):
        source = tmp_path / f"source{index}.c"
        output = tmp_path / f"generated{index}" / f"source{index}.c"
        source.write_text('#define defer(...) do {} while (0)\nvoid f(void) { defer((void)0); }\n', encoding="utf-8")
        records.append((source, output, ["-std=c17", f"-DVALUE={index}"]))

    def run() -> str:
        manifest.write_text("".join(f"{source}\t{output}\t{json.dumps(flags)}\n" for source, output, flags in records), encoding="utf-8")
        result = subprocess.run([binary, "--batch", str(manifest), "--dependency-stamps", "--incremental"],
                                capture_output=True, text=True, env=compiler_environment, check=False)
        assert result.returncode == 0, result.stderr
        return result.stderr

    assert "transformed 2 of 2" in run()
    assert "transformed 0 of 2" in run()
    records[0][2].append("-DCHANGED=1")
    assert "transformed 1 of 2" in run()


def test_per_file_batch_with_header_overlay_and_dependencies(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    header = tmp_path / "clean.h"
    header.write_text('#include <ascii-chat/tooling/defer/defer.h>\n'
                      'static inline void clean(int *n) { defer(*n = VALUE); }\n', encoding="utf-8")
    source = tmp_path / "input.c"
    source.write_text('#include "clean.h"\nint main(void) { int n=0; clean(&n); return n!=7; }\n', encoding="utf-8")
    output = tmp_path / "generated" / "input.c"
    manifest, depfile = tmp_path / "batch.txt", tmp_path / "batch.d"
    flags = ["-std=c17", "-DVALUE=7", "-I", str(tmp_path), "-I", str(ROOT / "include")]
    manifest.write_text(f"{source}\t{output}\t{json.dumps(flags)}\n", encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), "--batch", str(manifest), "--header-overlays", "--dependency-stamps",
         "--depfile", str(depfile)], capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    overlay = Path(str(output.parent) + ".vfs.json")
    stamp = Path(str(output.parent) + ".deps")
    assert overlay.is_file() and "clean.h" in stamp.read_text(encoding="utf-8")
    assert "clean.h" in depfile.read_text(encoding="utf-8")
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([str(CLANG), *flags, "-ivfsoverlay", str(overlay), str(output), "-o", str(executable)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_per_file_batch_honors_individual_working_directory(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    build = tmp_path / "build dir"
    include = build / "relative include"
    include.mkdir(parents=True)
    (include / "settings.h").write_text('#define VALUE 7\n', encoding="utf-8")
    source, output = tmp_path / "source.c", tmp_path / "output.c"
    source.write_text('#include "settings.h"\n#if VALUE != 7\n#error wrong include\n#endif\nvoid f(void) {}\n', encoding="utf-8")
    flags = ["-working-directory", str(build), "-I", "relative include"]
    manifest = tmp_path / "batch.txt"
    manifest.write_text(f"{source}\t{output}\t{json.dumps(flags)}\n", encoding="utf-8")
    result = subprocess.run([*tool_command(transform_environment), "--batch", str(manifest)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert output.is_file()


def test_cpp_defer_handles_scope_exceptions_and_cancellation(tmp_path: Path) -> None:
    compiler = shutil.which("clang++")
    if compiler is None:
        pytest.skip("Clang C++ compiler is required")
    source = tmp_path / "program.cpp"
    source.write_text(
        'extern "C" {\n#include <ascii-chat/tooling/defer/defer.h>\n}\n'
        'static int trace;\n'
        'struct Test { int run() { defer(trace = trace * 10 + 1); '
        '{ defer(trace = trace * 10 + 2); defer_named(skip, trace = 9); defer_cancel(skip); } '
        'return trace; } };\n'
        'int main() { Test t; if (t.run() != 2 || trace != 21) return 1; '
        'try { defer(trace = trace * 10 + 3); throw 1; } catch (...) {} '
        'return trace != 213; }\n', encoding="utf-8"
    )
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run(
        [compiler, "-std=c++11", "-I", str(ROOT / "include"), str(source), "-o", str(executable)],
        capture_output=True, text=True, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


@pytest.mark.parametrize("extension", ["cpp", "hpp", "h"])
def test_cpp_tool_output_preserves_exception_cleanup(
    tmp_path: Path, transform_environment: dict[str, str], extension: str
) -> None:
    compiler = shutil.which("clang++")
    if not compiler:
        pytest.skip("Clang C++ compiler is required")
    source = tmp_path / f"input.{extension}"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'static int value; struct A { void run() { defer(value = 4); throw 1; } };\n'
        'int main() { try { A().run(); } catch (...) {} return value != 4; }\n', encoding="utf-8"
    )
    output = tmp_path / f"output.{extension}"
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-x", "c++", "-std=c++11", "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run(
        [compiler, "-x", "c++", "-std=c++11", "-I", str(ROOT / "include"), str(output), "-o", str(executable)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_cpp_tool_rejects_invalid_defer(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source = tmp_path / "input.cpp"
    output = tmp_path / "output.cpp"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\nvoid f() { defer(unknown()); }\n', encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-std=c++11", "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert "unknown" in result.stderr
    assert not output.exists()


@pytest.mark.parametrize("extension", ["h", "hpp"])
def test_cpp_defer_in_included_header(tmp_path: Path, extension: str) -> None:
    compiler = shutil.which("clang++")
    if not compiler:
        pytest.skip("Clang C++ compiler is required")
    header = tmp_path / f"helper.{extension}"
    header.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'inline void clean(int &value) { defer(value = 7); throw 1; }\n', encoding="utf-8"
    )
    source = tmp_path / "main.cpp"
    source.write_text(
        f'#include "{header.name}"\n'
        'int main() { int value = 0; try { clean(value); } catch (...) {} return value != 7; }\n', encoding="utf-8"
    )
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run(
        [compiler, "-std=c++11", "-I", str(ROOT / "include"), str(source), "-o", str(executable)],
        capture_output=True, text=True, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


@pytest.mark.parametrize("extension", ["cpp", "hpp"])
@pytest.mark.parametrize("case", ["order", "loops", "cancel", "return", "exception", "template"])
def test_cpp_defer_behavior_matrix(
    tmp_path: Path, transform_environment: dict[str, str], extension: str, case: str
) -> None:
    compiler = shutil.which("clang++")
    if not compiler:
        pytest.skip("Clang C++ compiler is required")
    cases = {
        "order": 'int f() { defer(trace=trace*10+1); defer(trace=trace*10+2); '
                 '{ defer(trace=trace*10+3); } return trace; } '
                 'int main() { return f()!=3 || trace!=321; }',
        "loops": 'int main() { for(int i=0;i<3;++i) { defer(trace=trace*10+i+1); '
                 'if(i==0) continue; if(i==1) break; } return trace!=12; }',
        "cancel": 'int main() { { defer_named(a,trace=trace*10+1); '
                  'defer_named(b,trace=trace*10+2); defer_cancel(a); defer_cancel(a); } '
                  'return trace!=2; }',
        "return": 'int f() { int value=4; defer(value=9); return value++; } '
                  'int main() { return f()!=4; }',
        "exception": 'int main() { try { defer(trace=trace*10+1); '
                     '{ defer(trace=trace*10+2); throw 7; } } catch(int n) { '
                     'if(n!=7) return 1; } return trace!=21; }',
        "template": 'template <typename T> T f(T n) { defer(trace=trace*10+n); return n; } '
                    'int main() { if(f<int>(2)!=2 || f<long>(3)!=3) return 1; return trace!=23; }',
    }
    source = tmp_path / f"input.{extension}"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\nstatic int trace;\n' + cases[case] + '\n', encoding="utf-8")
    output = tmp_path / f"output.{extension}"
    flags = ["-x", "c++", "-std=c++11", "-I", str(ROOT / "include")]
    result = subprocess.run([*tool_command(transform_environment), str(source), str(output), "--", *flags],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([compiler, *flags, str(output), "-o", str(executable)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert result.returncode == 0, f"{case}: exit {result.returncode}: {result.stderr}"


@pytest.mark.parametrize("body", [
    'defer(no_such_function());',
    'defer_cancel(missing);',
    'defer_named(handle, (void)0); defer_named(handle, (void)0);',
    'goto label; defer((void)0); label:;',
])
@pytest.mark.parametrize("extension", ["cpp", "hpp"])
def test_cpp_tool_rejects_invalid_scope(
    tmp_path: Path, transform_environment: dict[str, str], body: str, extension: str
) -> None:
    source, output = tmp_path / f"input.{extension}", tmp_path / f"output.{extension}"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\nvoid f() { ' + body + ' }\n', encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-x", "c++", "-std=c++11", "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert not output.exists()


def test_cpp_compiler_rejects_goto_past_guard(tmp_path: Path) -> None:
    compiler = shutil.which("clang++")
    if not compiler:
        pytest.skip("Clang C++ compiler is required")
    source = tmp_path / "source.cpp"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\n'
                      'void f() { goto end; defer((void)0); end:; }\n', encoding="utf-8")
    result = subprocess.run([compiler, "-std=c++11", "-fsyntax-only", "-I", str(ROOT / "include"), str(source)],
                            capture_output=True, text=True, check=False)
    assert result.returncode != 0
    assert "goto" in result.stderr


def test_cpp_header_across_translation_units(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    compiler = shutil.which("clang++")
    if not compiler:
        pytest.skip("Clang C++ compiler is required")
    header = tmp_path / "input.hpp"
    header.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'template<class T> inline T clean(T &v, bool fail) { '
        'defer(v = v*10+1); { defer_named(skip, v = 99); defer_cancel(skip); '
        'defer(v = v*10+2); if(fail) throw 7; } return v; }\n', encoding="utf-8"
    )
    output = tmp_path / "output.hpp"
    result = subprocess.run(
        [*tool_command(transform_environment), str(header), str(output), "--", "-x", "c++", "-std=c++11", "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    (tmp_path / "a.cpp").write_text('#include "output.hpp"\nint a() { int v=0; int r=clean(v,false); return r==2 && v==21; }\n', encoding="utf-8")
    (tmp_path / "b.cpp").write_text('#include "output.hpp"\nint b() { long v=0; try { clean(v,true); } '
                                      'catch(int n) { return n==7 && v==21; } return 0; }\n', encoding="utf-8")
    (tmp_path / "main.cpp").write_text('int a(); int b(); int main() { return !a() || !b(); }\n', encoding="utf-8")
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run(
        [compiler, "-std=c++11", "-I", str(ROOT / "include"), str(tmp_path / "a.cpp"), str(tmp_path / "b.cpp"),
         str(tmp_path / "main.cpp"), "-o", str(executable)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert result.returncode == 0, f"exit {result.returncode}: {result.stderr}"


@pytest.mark.parametrize("extension", ["h", "hpp"])
def test_c_header_as_direct_tool_input(
    tmp_path: Path, transform_environment: dict[str, str], extension: str
) -> None:
    source = tmp_path / f"input.{extension}"
    output = tmp_path / f"output.{extension}"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'static inline void clean(int *value) { defer(*value = 7); }\n', encoding="utf-8"
    )
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-x", "c", "-std=c17", "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    program = tmp_path / "main.c"
    program.write_text(f'#include "{output.name}"\nint main(void) {{ int n = 0; clean(&n); return n != 7; }}\n', encoding="utf-8")
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    compiled = subprocess.run([str(CLANG), "-std=c17", "-I", str(ROOT / "include"), str(program), "-o", str(executable)],
                              capture_output=True, text=True, check=False)
    assert compiled.returncode == 0, compiled.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


@pytest.mark.parametrize("wrapped", [False, True])
@pytest.mark.parametrize("extension", ["h", "hpp"])
def test_header_overlay_executes_cleanup(
    tmp_path: Path, transform_environment: dict[str, str], wrapped: bool, extension: str
) -> None:
    if not CLANG:
        pytest.skip("Clang is required")
    header = tmp_path / f"header.{extension}"
    header.write_text(
        '#if ENABLE_CLEANUP\nstatic inline void clean(int *value) { '
        + ('WRAPPED(*value = 7);' if wrapped else 'defer(*value = 7);')
        + ' }\n#endif\n', encoding="utf-8"
    )
    source = tmp_path / "source.c"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n#define ENABLE_CLEANUP 1\n'
        f'#define WRAPPED(action) defer(action)\n#include "{header.name}"\n'
        'int main(void) { int value = 0; clean(&value); return value != 7; }\n', encoding="utf-8"
    )
    output = tmp_path / "generated" / "source.c"
    overlay = tmp_path / "generated" / "overlay.json"
    result = subprocess.run(
        [*tool_command(transform_environment), "--header-overlay", str(overlay), str(source), str(output), "--", "-std=c17", "-I", str(ROOT / "include"), "-I", str(tmp_path)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    mapped = json.loads(overlay.read_text(encoding="utf-8"))["roots"]
    assert any(Path(item["external-contents"]).suffix == f".{extension}" for item in mapped)
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run(
        [CLANG, "-std=c17", "-ivfsoverlay", str(overlay), "-I", str(ROOT / "include"), "-I", str(tmp_path), str(output), "-o", str(executable)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0
    assert ("WRAPPED(" if wrapped else "defer(") in header.read_text(encoding="utf-8")


def test_header_overlays_are_per_translation_unit(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    if not CLANG:
        pytest.skip("Clang is required")
    header = tmp_path / "header.h"
    header.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'static inline void clean(int *value) { defer(*value = MODE); }\n', encoding="utf-8"
    )
    objects = []
    for number in (1, 2):
        source = tmp_path / f"source{number}.c"
        source.write_text(
            f'#define MODE {number}\n#include "header.h"\n'
            f'int f{number}(void) {{ int result = 0; clean(&result); return result; }}\n', encoding="utf-8"
        )
        output = tmp_path / f"generated{number}" / "source.c"
        overlay = tmp_path / f"generated{number}" / "overlay.json"
        result = subprocess.run(
            [*tool_command(transform_environment), "--header-overlay", str(overlay), str(source), str(output), "--", "-std=c17", "-I", str(ROOT / "include"), "-I", str(tmp_path)],
            capture_output=True, text=True, env=transform_environment, check=False,
        )
        assert result.returncode == 0, result.stderr
        obj = tmp_path / f"source{number}.o"
        result = subprocess.run(
            [CLANG, "-std=c17", "-ivfsoverlay", str(overlay), "-I", str(ROOT / "include"), "-I", str(tmp_path), "-c", str(output), "-o", str(obj)],
            capture_output=True, text=True, env=transform_environment, check=False,
        )
        assert result.returncode == 0, result.stderr
        objects.append(str(obj))
    main = tmp_path / "main.c"
    main.write_text('int f1(void); int f2(void); int main(void) { return f1() != 1 || f2() != 2; }\n', encoding="utf-8")
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([CLANG, str(main), *objects, "-o", str(executable)], capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_header_overlay_does_not_overwrite_header(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    header = tmp_path / "header.h"
    original = '#include <ascii-chat/tooling/defer/defer.h>\nstatic inline void f(void) { defer((void)0); }\n'
    header.write_text(original, encoding="utf-8")
    source = tmp_path / "source.c"
    source.write_text('#include "header.h"\n', encoding="utf-8")
    output = tmp_path / "output.c"
    result = subprocess.run(
        [*tool_command(transform_environment), "--header-overlay", str(header), str(source), str(output), "--", "-std=c17", "-I", str(ROOT / "include"), "-I", str(tmp_path)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert header.read_text(encoding="utf-8") == original
    assert not output.exists()


def test_tool_output_cannot_overwrite_included_header(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    header = tmp_path / "header.h"
    original = '#define VALUE 7\n'
    header.write_text(original, encoding="utf-8")
    source = tmp_path / "source.c"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n#include "header.h"\n'
        'void f(int *n) { defer(*n = VALUE); }\n', encoding="utf-8"
    )
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(header), "--", "-std=c17", "-I", str(ROOT / "include"), "-I", str(tmp_path)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert "included file" in result.stderr
    assert header.read_text(encoding="utf-8") == original


def test_header_overlay_preserves_nested_relative_includes(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    if not CLANG:
        pytest.skip("Clang is required")
    nested_dir = tmp_path / "nested"
    nested_dir.mkdir()
    (nested_dir / "base.h").write_text('#include "../cleanup.h"\n', encoding="utf-8")
    (tmp_path / "cleanup.h").write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\nstatic inline void clean(int *n) { defer(*n = 3); }\n',
        encoding="utf-8",
    )
    source = tmp_path / "source.c"
    source.write_text('#include "nested/base.h"\nint main(void) { int n = 0; clean(&n); return n != 3; }\n', encoding="utf-8")
    output = tmp_path / "out" / "source.c"
    overlay = tmp_path / "out" / "overlay.json"
    result = subprocess.run(
        [*tool_command(transform_environment), "--header-overlay", str(overlay), str(source), str(output), "--", "-std=c17", "-I", str(ROOT / "include"), "-I", str(tmp_path)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run(
        [CLANG, "-std=c17", "-ivfsoverlay", str(overlay), "-I", str(ROOT / "include"), "-I", str(tmp_path), str(output), "-o", str(executable)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_header_overlay_resolves_symlinked_header(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    if not CLANG:
        pytest.skip("Clang is required")
    header = tmp_path / "real.h"
    header.write_text(
        '#pragma once\n#include <ascii-chat/tooling/defer/defer.h>\n'
        'static inline void clean(int *n, const char **name) { defer({ *n = 3; *name = __FILE__; }); }\n', encoding="utf-8"
    )
    alias = tmp_path / "alias.h"
    try:
        alias.symlink_to(header)
    except OSError:
        pytest.skip("file symlinks are not available")
    source = tmp_path / "source.c"
    source.write_text('#include "alias.h"\n#include "real.h"\nint puts(const char *);\nint main(void) { int n=0; const char *name=0; clean(&n, &name); puts(name); return n!=3; }\n', encoding="utf-8")
    output = tmp_path / "out" / "source.c"
    overlay = tmp_path / "out" / "overlay.json"
    result = subprocess.run(
        [*tool_command(transform_environment), "--header-overlay", str(overlay), str(source), str(output), "--", "-std=c17", "-I", str(ROOT / "include"), "-I", str(tmp_path)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    header.write_text(
        '#pragma once\nstatic inline void clean(int *n, const char **name) { *n = 3; *name = __FILE__; }\n',
        encoding="utf-8",
    )
    baseline = tmp_path / ("baseline.exe" if sys.platform == "win32" else "baseline")
    result = subprocess.run(
        [CLANG, "-std=c17", "-I", str(tmp_path), str(source), "-o", str(baseline)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    baseline_result = subprocess.run([str(baseline)], capture_output=True, text=True, check=False)
    assert baseline_result.returncode == 0
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run(
        [CLANG, "-std=c17", "-ivfsoverlay", str(overlay), "-I", str(ROOT / "include"), "-I", str(tmp_path), str(output), "-o", str(executable)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    generated_result = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert generated_result.returncode == 0
    assert generated_result.stdout == baseline_result.stdout


def test_header_overlay_rejects_invalid_cleanup_before_writing(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    header = tmp_path / "header.h"
    header.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\nstatic inline void clean(void) { defer(undefined_name()); }\n',
        encoding="utf-8",
    )
    source = tmp_path / "source.c"
    source.write_text('#include "header.h"\n', encoding="utf-8")
    output = tmp_path / "out" / "source.c"
    overlay = tmp_path / "out" / "overlay.json"
    result = subprocess.run(
        [*tool_command(transform_environment), "--header-overlay", str(overlay), str(source), str(output), "--", "-std=c17", "-I", str(ROOT / "include"), "-I", str(tmp_path)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert "undefined_name" in result.stderr
    assert not output.exists()
    assert not overlay.exists()


def test_header_overlay_rejects_repeated_header_instantiations(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    header = tmp_path / "header.h"
    header.write_text('static inline void FUNCTION(int *n) { defer((*n)++); }\n', encoding="utf-8")
    source = tmp_path / "source.c"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        '#define FUNCTION first\n#include "header.h"\n#undef FUNCTION\n'
        '#define FUNCTION second\n#include "header.h"\n', encoding="utf-8"
    )
    output = tmp_path / "out" / "source.c"
    overlay = tmp_path / "out" / "overlay.json"
    result = subprocess.run(
        [*tool_command(transform_environment), "--header-overlay", str(overlay), str(source), str(output), "--", "-std=c17", "-I", str(ROOT / "include"), "-I", str(tmp_path)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert "instantiated multiple times" in result.stderr
    assert not output.exists() and not overlay.exists()


@pytest.mark.parametrize("symlinked", [False, True])
def test_header_overlay_batch_tracks_dependencies(
    tmp_path: Path, transform_environment: dict[str, str], symlinked: bool
) -> None:
    if not CLANG:
        pytest.skip("Clang is required")
    native = bool(transform_environment.get("ASCIICHAT_DEFER_TEST_BINARY"))
    header = tmp_path / "header.h"
    header.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'static inline void clean(int *n) { defer(*n = MODE); }\n', encoding="utf-8"
    )
    include_name = header.name
    if symlinked:
        alias = tmp_path / "alias.h"
        try:
            alias.symlink_to(header)
        except OSError:
            pytest.skip("file symlinks are not available")
        include_name = alias.name
    records = []
    commands = []
    outputs = []
    for number in (1, 2):
        source = tmp_path / f"source{number}.c"
        source.write_text(f'#define MODE {number}\n#include "{include_name}"\nint f{number}(void) {{ int n=0; clean(&n); return n; }}\n', encoding="utf-8")
        output = tmp_path / "out" / f"source{number}.c.defer" / f"source{number}.c"
        outputs.append(output)
        records.append(f"{source}\t{output}\n")
        overlay = Path(str(output.parent) + ".vfs.json")
        commands.append({"directory": str(tmp_path), "file": str(output), "arguments": [
            CLANG, "-std=c17", "-I", str(ROOT / "include"), "-I", str(tmp_path), "-ivfsoverlay", str(overlay), "-c", str(output), "-o", str(tmp_path / f"source{number}.o")
        ]})
    manifest = tmp_path / "batch.txt"
    manifest.write_text("".join(records), encoding="utf-8")
    (tmp_path / "compile_commands.json").write_text(json.dumps(commands), encoding="utf-8")
    depfile = tmp_path / "batch.d"
    command = [*tool_command(transform_environment), "--batch", str(manifest), "--header-overlays", "--compilation-database", str(tmp_path), "--depfile", str(depfile), "--dependency-stamps"]
    command += ["--incremental"] if native else ["--libclang", transform_environment["LIBCLANG_LIBRARY_FILE"]]
    result = subprocess.run(command, capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    if native:
        assert "transformed 2 of 2" in result.stderr
    assert "header.h" in depfile.read_text(encoding="utf-8") or "alias.h" in depfile.read_text(encoding="utf-8")
    result = subprocess.run(command, capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    if native:
        assert "transformed 0 of 2" in result.stderr
    if symlinked:
        alternate = tmp_path / "alternate.h"
        alternate.write_bytes(header.read_bytes())
        stat = header.stat()
        os.utime(alternate, ns=(stat.st_atime_ns, stat.st_mtime_ns))
        alias.unlink()
        alias.symlink_to(alternate)
        result = subprocess.run(command, capture_output=True, text=True, env=transform_environment, check=False)
        assert result.returncode == 0, result.stderr
        if native:
            assert "transformed 2 of 2" in result.stderr
        assert "alternate.h" in Path(str(outputs[0].parent) + ".deps").read_text(encoding="utf-8")
    second_output_time = outputs[1].stat().st_mtime_ns
    second_stamp = Path(str(outputs[1].parent) + ".deps")
    second_stamp_time = second_stamp.stat().st_mtime_ns
    time.sleep(1.1)
    first_source = tmp_path / "source1.c"
    first_source.write_text(first_source.read_text(encoding="utf-8") + "/* changed */\n", encoding="utf-8")
    result = subprocess.run(command, capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    if native:
        assert "transformed 1 of 2" in result.stderr
    assert outputs[1].stat().st_mtime_ns == second_output_time
    assert second_stamp.stat().st_mtime_ns == second_stamp_time
    first_overlay = Path(str(outputs[0].parent) + ".vfs.json")
    generated_header = Path(json.loads(first_overlay.read_text(encoding="utf-8"))["roots"][0]["external-contents"])
    generated_header.unlink()
    result = subprocess.run(command, capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    if native:
        assert "transformed 1 of 2" in result.stderr
    assert generated_header.is_file()
    objects = []
    for number, output in enumerate(outputs, 1):
        overlay = Path(str(output.parent) + ".vfs.json")
        stamp = Path(str(output.parent) + ".deps")
        assert overlay.is_file() and stamp.is_file()
        assert "header.h" in stamp.read_text(encoding="utf-8") or "alias.h" in stamp.read_text(encoding="utf-8")
        obj = tmp_path / f"object{number}.o"
        result = subprocess.run(
            [CLANG, "-std=c17", "-ivfsoverlay", str(overlay), "-I", str(ROOT / "include"), "-I", str(tmp_path), "-c", str(output), "-o", str(obj)],
            capture_output=True, text=True, env=transform_environment, check=False,
        )
        assert result.returncode == 0, result.stderr
        objects.append(str(obj))
    main = tmp_path / "main.c"
    main.write_text('int f1(void); int f2(void); int main(void) { return f1()!=1 || f2()!=2; }\n', encoding="utf-8")
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([CLANG, str(main), *objects, "-o", str(executable)], capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_header_overlay_batch_cannot_replace_other_source(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    first = tmp_path / "first.c"
    first.write_text("void first(void) {}\n", encoding="utf-8")
    other = tmp_path / "generated.vfs.json"
    other.write_text("void other(void) {}\n", encoding="utf-8")
    manifest = tmp_path / "batch.txt"
    manifest.write_text(f"{first}\t{tmp_path / 'generated' / 'first.c'}\n{other}\t{tmp_path / 'other' / 'other.c'}\n", encoding="utf-8")
    command = [*tool_command(transform_environment), "--batch", str(manifest), "--header-overlays"]
    if not transform_environment.get("ASCIICHAT_DEFER_TEST_BINARY"):
        command += ["--libclang", transform_environment["LIBCLANG_LIBRARY_FILE"], "--compilation-database", str(tmp_path), "--depfile", str(tmp_path / "batch.d")]
    result = subprocess.run(command, capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode != 0
    assert "header overlay must not overwrite" in result.stderr
    assert other.read_text(encoding="utf-8") == "void other(void) {}\n"


def test_header_overlay_native_and_python_outputs_match(tmp_path: Path, compiler_environment: dict[str, str]) -> None:
    binary = compiler_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not binary:
        pytest.skip("native defer binary was not supplied")
    header = tmp_path / "header.h"
    header.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        '#define CLEANUP(action) defer(action)\n'
        'static inline void clean(int *n) { CLEANUP(*n = 7); }\n', encoding="utf-8"
    )
    source = tmp_path / "source.c"
    source.write_text('#include "header.h"\nint main(void) { int n=0; clean(&n); return n!=7; }\n', encoding="utf-8")
    outputs: list[bytes] = []
    headers: list[bytes] = []
    for name, command in (("native", [binary]), ("python", [sys.executable, str(TOOL)])):
        output = tmp_path / name / "source.c"
        overlay = tmp_path / name / "overlay.json"
        result = subprocess.run(
            [*command, "--header-overlay", str(overlay), str(source), str(output), "--", "-std=c17", "-I", str(ROOT / "include"), "-I", str(tmp_path)],
            capture_output=True, text=True, env=compiler_environment, check=False,
        )
        assert result.returncode == 0, result.stderr
        outputs.append(output.read_bytes())
        mapped = json.loads(overlay.read_text(encoding="utf-8"))["roots"]
        headers.append(Path(mapped[0]["external-contents"]).read_bytes())
    assert outputs[0] == outputs[1]
    assert headers[0] == headers[1]


@pytest.mark.parametrize("backend", ["NATIVE", "PYTHON"])
@pytest.mark.parametrize("extension", ["h", "hpp"])
@pytest.mark.parametrize("mode", ["ON", "AUTO"])
def test_cmake_experimental_header_overlays_are_incremental(
    tmp_path: Path, compiler_environment: dict[str, str], backend: str, extension: str, mode: str
) -> None:
    binary = compiler_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    cmake = shutil.which("cmake")
    clangxx = shutil.which("clang++")
    if not binary or not cmake or not clangxx or not CLANG or not shutil.which("ninja"):
        pytest.skip("native defer binary, CMake, Ninja, and Clang are required")
    src = tmp_path / "src"
    src.mkdir()
    if backend == "PYTHON":
        tool_dir = src / "tooling" / "defer"
        tool_dir.mkdir(parents=True)
        for name in ("defer.py", "analysis.py", "manifest.py", "plan.py", "scan.py", "pyproject.toml", "uv.lock"):
            shutil.copy2(TOOL.parent / name, tool_dir / name)
        shutil.copytree(TOOL.parent / "vendor" / "clang", tool_dir / "vendor" / "clang")
    header = src / f"clean.{extension}"
    header.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'static inline void clean(int *n) { *n = MODE; }\n', encoding="utf-8"
    )
    (src / "main.c").write_text(
        f'#define MODE 7\n#include "{header.name}"\nint other(void);\n'
        'int main(void) { int n=0; clean(&n); return n!=7 || other()!=42; }\n', encoding="utf-8"
    )
    other = src / "other.c"
    other.write_text('int other(void) { return 42; }\n', encoding="utf-8")
    module = (ROOT / "cmake" / "tooling" / "Defer.cmake").as_posix()
    (tmp_path / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.30)\nproject(defer_header C CXX)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        f'include("{module}")\nadd_subdirectory(src)\nascii_defer_prepare()\n', encoding="utf-8"
    )
    (src / "CMakeLists.txt").write_text(
        'add_executable(ascii-chat-probe main.c other.c)\n'
        + (f'target_precompile_headers(ascii-chat-probe PRIVATE "${{CMAKE_CURRENT_SOURCE_DIR}}/{header.name}")\n' if mode == "ON" else "")
        +
        f'target_include_directories(ascii-chat-probe PRIVATE "{(ROOT / "include").as_posix()}")\n', encoding="utf-8"
    )
    build = tmp_path / "out"

    def run(*command: str) -> str:
        result = subprocess.run(command, capture_output=True, text=True, env=compiler_environment, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout + result.stderr

    run(cmake, "-S", str(tmp_path), "-B", str(build), "-G", "Ninja", f"-DCMAKE_C_COMPILER={CLANG}",
        f"-DCMAKE_CXX_COMPILER={clangxx}", f"-DASCIICHAT_DEFER_NATIVE_BINARY={binary}",
        f"-DASCIICHAT_DEFER_BACKEND={backend}", f"-DASCIICHAT_LIBCLANG_PATH={compiler_environment['LIBCLANG_LIBRARY_FILE']}",
        f"-DASCIICHAT_DEFER_HEADER_OVERLAYS={mode}")
    first_build = run(cmake, "--build", str(build))
    if mode == "ON":
        assert "Transforming defer source batch" in first_build
    else:
        assert "Transforming defer source batch" not in first_build
    if backend == "NATIVE" and mode == "ON":
        assert "transformed 2 of 2" in first_build
    executable = build / "src" / ("ascii-chat-probe.exe" if sys.platform == "win32" else "ascii-chat-probe")
    run(str(executable))
    objects = list((build / "src" / "CMakeFiles").rglob("*.obj" if sys.platform == "win32" else "*.o"))
    main_obj = next(path for path in objects if "main.c" in path.name)
    other_obj = next(path for path in objects if "other.c" in path.name)
    other_mtime = other_obj.stat().st_mtime_ns
    main_mtime = main_obj.stat().st_mtime_ns
    time.sleep(1.1)
    header.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'static inline void clean(int *n) { defer(*n = MODE); }\n', encoding="utf-8"
    )
    output = run(cmake, "--build", str(build))
    if mode == "AUTO":
        assert "Re-running CMake" in output
        objects = list((build / "src" / "CMakeFiles").rglob("*.obj" if sys.platform == "win32" else "*.o"))
        main_obj = next(path for path in objects if "defer_transformed" in str(path) and "main.c" in path.name)
        other_obj = next(path for path in objects if "defer_transformed" in str(path) and "other.c" in path.name)
        main_mtime = main_obj.stat().st_mtime_ns
        other_mtime = other_obj.stat().st_mtime_ns
        time.sleep(1.1)
        header.write_text(header.read_text(encoding="utf-8") + "/* header edit */\n", encoding="utf-8")
        output = run(cmake, "--build", str(build))
    if backend == "NATIVE":
        assert "transformed 1 of 2" in output
    assert main_obj.stat().st_mtime_ns > main_mtime
    assert other_obj.stat().st_mtime_ns == other_mtime
    run(str(executable))
    assert "Transforming defer source batch" not in run(cmake, "--build", str(build))


@pytest.mark.parametrize(("contents", "expected"), [
    ('static inline void f(void) { defer((void)0); }\n', True),
    ('static inline void f(void) { WRAPPED((void)0); }\n', True),
    ('static inline void f(void) { de\\\nfer((void)0); }\n', True),
    ('/* defer((void)0); */\nconst char *s = "defer((void)0)";\n', False),
    ('#define WRAPPED(x) \\\n defer(x)\n/*\n defer((void)0);\n*/\n', False),
    ('/* a comment */ static inline void f(void) { defer((void)0); }\n', True),
    ('const char *s = "/* ; */ defer(x)"; /* defer(x); */\n', False),
    ('static inline void f(void) { int deferred_value = 0; }\n', False),
    ("static inline void f(void) { char c = '\\\''; defer((void)c); }\n", True),
    ('/* comment begins\n#define NOT_CODE defer(x)\nends */ static inline void f(void) { defer((void)0); }\n', True),
])
def test_cmake_header_detection_ignores_non_code(tmp_path: Path, contents: str, expected: bool) -> None:
    cmake = shutil.which("cmake")
    if not cmake:
        pytest.skip("CMake is required")
    header = tmp_path / "header.h"
    header.write_text(contents, encoding="utf-8")
    script = tmp_path / "scan.cmake"
    module = (ROOT / "cmake" / "tooling" / "DeferHeaderScan.cmake").as_posix()
    script.write_text(
        f'include("{module}")\nfile(READ "{header.as_posix()}" text)\n'
        'string(REGEX REPLACE "\\\\\\\\\\n" "" text "${text}")\n'
        'set(wrappers defer WRAPPED)\nascii_defer_header_has_code(found text wrappers)\nmessage("found=${found}")\n',
        encoding="utf-8",
    )
    result = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"found={'TRUE' if expected else 'FALSE'}" in result.stderr


@pytest.mark.parametrize("flags", [[], ["-fobjc-exceptions", "-fno-objc-exceptions"], ["-fobjc-exceptions", "-fno-exceptions"]])
def test_objc_tool_requires_exception_support(tmp_path: Path, transform_environment: dict[str, str], flags: list[str]) -> None:
    source = tmp_path / "source.m"
    output = tmp_path / "output.m"
    source.write_text('void f(void) { defer((void)0); }\n', encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", *flags],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert "-fobjc-exceptions" in result.stderr
    assert not output.exists()


def test_objc_tool_rejects_legacy_exception_runtime(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source = tmp_path / "source.m"
    output = tmp_path / "output.m"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\nvoid f(void) { defer((void)0); }\n', encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-fobjc-exceptions",
         "-fobjc-runtime=gcc", "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert "zero-cost exceptions" in result.stderr
    assert not output.exists()


def test_objc_functions_and_methods_transform(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source = tmp_path / "source.m"
    output = tmp_path / "output.m"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'int value; int f(int x) { defer(value += x); { defer_named(handle, value += 2); '
        'if (x) defer_cancel(handle); if (x > 2) return x++; } return x; }\n'
        '__attribute__((objc_root_class)) @interface Root\n- (int)method:(int)x;\n@end\n'
        '@implementation Root\n- (int)method:(int)x { defer(value += x); return x; }\n@end\n',
        encoding="utf-8",
    )
    flags = ["-fobjc-exceptions", *objc_syntax_runtime(), "-I", str(ROOT / "include")]
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", *flags],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    generated = output.read_text(encoding="utf-8")
    assert generated.count("@finally") == 3
    result = subprocess.run([str(CLANG), "-fsyntax-only", *flags, str(output)], capture_output=True, text=True,
                            env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr


def test_objc_block_defer_is_scoped_to_invocation(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source = tmp_path / "source.m"
    output = tmp_path / "output.m"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'int value; void f(void) { defer(value += 1); void (^block)(void) = ^{ defer(value += 2); return; }; block(); }\n',
        encoding="utf-8",
    )
    flags = ["-fobjc-exceptions", *objc_syntax_runtime(), "-fblocks", "-I", str(ROOT / "include")]
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", *flags],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert output.read_text(encoding="utf-8").count("@finally") == 2


def test_objc_returned_block_has_its_own_defer(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source, output = tmp_path / "source.m", tmp_path / "output.m"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\n'
                      'int value; void (^f(void))(void) { return ^{ defer(value++); }; }\n', encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-fobjc-exceptions", "-fblocks",
         *objc_syntax_runtime(), "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "@finally" in output.read_text(encoding="utf-8")


def test_objc_global_block_has_its_own_defer(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source, output = tmp_path / "source.m", tmp_path / "output.m"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\n'
                      'int value; void (^block)(void) = ^{ defer(value++); };\n', encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-fobjc-exceptions", "-fblocks",
         *objc_syntax_runtime(), "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "@finally" in output.read_text(encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), "--show-transformed", f"{source}:2", "--", "-fobjc-exceptions",
         "-fblocks", *objc_syntax_runtime(), "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "@finally" in result.stdout


@pytest.mark.parametrize("inspection", ["--explain", "--show-transformed"])
def test_objc_method_inspection(tmp_path: Path, transform_environment: dict[str, str], inspection: str) -> None:
    source = tmp_path / "source.m"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        '__attribute__((objc_root_class)) @interface Root\n- (int)run;\n@end\n'
        '@implementation Root\n- (int)run {\n  defer((void)0);\n  return 3;\n}\n@end\n', encoding="utf-8"
    )
    result = subprocess.run(
        [*tool_command(transform_environment), inspection, f"{source}:8", "--", "-fobjc-exceptions",
         *objc_syntax_runtime(), "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "(void)0" in result.stdout
    if inspection == "--show-transformed":
        assert "@finally" in result.stdout


def test_objc_header_overlay(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    header = tmp_path / "cleanup.h"
    header.write_text('#include <ascii-chat/tooling/defer/defer.h>\n'
                      'static inline void cleanup(int *n) { defer(*n = 4); }\n', encoding="utf-8")
    source = tmp_path / "source.m"
    source.write_text('#include "cleanup.h"\nvoid f(int *n) { cleanup(n); }\n', encoding="utf-8")
    output, overlay = tmp_path / "output.m", tmp_path / "overlay.json"
    result = subprocess.run(
        [*tool_command(transform_environment), "--header-overlay", str(overlay), str(source), str(output), "--",
         "-fobjc-exceptions", *objc_syntax_runtime(), "-I", str(ROOT / "include"), "-I", str(tmp_path)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "@finally" in next((tmp_path / "overlay.json.headers").iterdir()).read_text(encoding="utf-8")


def test_objc_goto_into_protected_region_fails_closed(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source, output = tmp_path / "source.m", tmp_path / "output.m"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\n'
                      'void f(int n) { if (n) goto target; defer((void)0); target: return; }\n', encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-fobjc-exceptions",
         *objc_syntax_runtime(), "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert "generated Objective-C is invalid" in result.stderr
    assert not output.exists()


@pytest.mark.parametrize("body", [
    'defer(no_such_function());',
    'defer_cancel(missing);',
    'defer_named(handle, (void)0); defer_named(handle, (void)0);',
    'defer({ return; });',
    'defer({ defer((void)0); });',
    'again: defer((void)0); goto again;',
])
def test_objc_invalid_cleanup_preserves_existing_output(
    tmp_path: Path, transform_environment: dict[str, str], body: str
) -> None:
    source, output = tmp_path / "source.m", tmp_path / "output.m"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\nvoid f(void) { ' + body + ' }\n', encoding="utf-8")
    output.write_text('existing output\n', encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-fobjc-exceptions",
         *objc_syntax_runtime(), "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert output.read_text(encoding="utf-8") == 'existing output\n'


def test_objc_fast_enumeration_cleanup(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source, output = tmp_path / "source.m", tmp_path / "output.m"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\n'
                      'void f(id collection) { for (id item in collection) { '
                      'defer((void)item); if (item) break; continue; } }\n', encoding="utf-8")
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-fobjc-exceptions",
         *objc_syntax_runtime(), "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "@finally" in output.read_text(encoding="utf-8")


def test_objc_native_and_python_outputs_match(tmp_path: Path, compiler_environment: dict[str, str]) -> None:
    binary = compiler_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    if not binary:
        pytest.skip("native defer binary was not supplied")
    source = tmp_path / "source.m"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\n'
                      'int v; int f(int n) { defer(v++); '
                      'void (^block)(void) = ^{ defer(v += 2); }; block(); return n; }\n', encoding="utf-8")
    outputs = []
    for command in ([binary], [sys.executable, str(TOOL)]):
        output = tmp_path / f"output{len(outputs)}.m"
        result = subprocess.run(
            [*command, str(source), str(output), "--", "-fobjc-exceptions", "-fblocks",
             *objc_syntax_runtime(), "-I", str(ROOT / "include")],
            capture_output=True, text=True, env=compiler_environment, check=False,
        )
        assert result.returncode == 0, result.stderr
        outputs.append(output.read_bytes())
    assert outputs[0] == outputs[1]


@pytest.mark.skipif(sys.platform not in ("darwin", "linux"), reason="requires an Objective-C runtime")
@pytest.mark.parametrize("stream", [False, True])
def test_objc_runtime_cleanup(tmp_path: Path, transform_environment: dict[str, str], stream: bool) -> None:
    source = ROOT / "tests" / "standalone" / "defer_objc_runtime.m"
    output = tmp_path / "transformed.m"
    resource_dir = subprocess.run([str(CLANG), "-print-resource-dir"], capture_output=True, text=True, check=True).stdout.strip()
    flags = ["-fobjc-exceptions", "-fblocks", "-I", str(ROOT / "include"), "-resource-dir", resource_dir]
    if sys.platform == "linux":
        runtime_root = transform_environment.get("ASCIICHAT_TEST_OBJC2_ROOT")
        if not runtime_root:
            pytest.skip("set ASCIICHAT_TEST_OBJC2_ROOT to a GNUstep libobjc2 installation")
        flags += ["-fobjc-runtime=gnustep-2.0", "-DASCIICHAT_TEST_GNUSTEP_OBJC", "-I", str(Path(runtime_root) / "include")]
        link_flags = ["-L", str(Path(runtime_root) / "lib"), f"-Wl,-rpath,{Path(runtime_root) / 'lib'}", "-lobjc"]
    else:
        link_flags = ["-framework", "Foundation"]
    if stream:
        streamed = subprocess.run(
            [*tool_command(transform_environment), "--filename", str(source), "--", *flags],
            input=source.read_bytes(), capture_output=True, env=transform_environment, check=False,
        )
        assert streamed.returncode == 0, streamed.stderr.decode(errors="replace")
        output.write_bytes(streamed.stdout)
    else:
        result = subprocess.run(
            [*tool_command(transform_environment), str(source), str(output), "--", *flags],
            capture_output=True, text=True, env=transform_environment, check=False,
        )
        assert result.returncode == 0, result.stderr
    executable = tmp_path / "objc-runtime-test"
    result = subprocess.run(
        [str(CLANG), *flags, str(output), *link_flags, "-o", str(executable)],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
    assert result.returncode == 0, f"exit {result.returncode}: {result.stderr}"


@pytest.mark.skipif(sys.platform != "darwin", reason="requires the macOS Objective-C runtime")
@pytest.mark.parametrize("backend", ["NATIVE", "PYTHON"])
@pytest.mark.parametrize("header_mode", [False, True])
def test_cmake_objc_incremental(tmp_path: Path, compiler_environment: dict[str, str], backend: str, header_mode: bool) -> None:
    binary = compiler_environment.get("ASCIICHAT_DEFER_TEST_BINARY")
    cmake = shutil.which("cmake")
    clangxx = shutil.which("clang++")
    if not binary or not cmake or not CLANG or not clangxx or not shutil.which("ninja"):
        pytest.skip("native defer binary, CMake, Ninja, and Clang are required")
    src = tmp_path / "src"
    src.mkdir()
    if backend == "PYTHON":
        tool_dir = src / "tooling" / "defer"
        tool_dir.mkdir(parents=True)
        for name in ("defer.py", "analysis.py", "manifest.py", "plan.py", "scan.py", "pyproject.toml", "uv.lock"):
            shutil.copy2(TOOL.parent / name, tool_dir / name)
        shutil.copytree(TOOL.parent / "vendor" / "clang", tool_dir / "vendor" / "clang")
    source = src / "main.m"
    header = src / "cleanup.h"
    if header_mode:
        header.write_text('static inline void clean(int *v) { *v = 8; }\n', encoding="utf-8")
        source.write_text('#include "cleanup.h"\nstatic int v; static int f(void) { clean(&v); return 1; }\n'
                          'int main(void) { return f() != 1 || v != 8; }\n', encoding="utf-8")
    else:
        source.write_text('int main(void) { return 0; }\n', encoding="utf-8")
    module = (ROOT / "cmake" / "tooling" / "Defer.cmake").as_posix()
    (tmp_path / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.30)\nproject(defer_objc C OBJC CXX)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        f'include("{module}")\nadd_subdirectory(src)\nascii_defer_prepare()\n', encoding="utf-8"
    )
    (src / "CMakeLists.txt").write_text(
        'add_executable(ascii-chat-probe main.m)\n'
        f'target_include_directories(ascii-chat-probe PRIVATE "{(ROOT / "include").as_posix()}")\n'
        'target_link_libraries(ascii-chat-probe PRIVATE "-framework Foundation")\n', encoding="utf-8"
    )
    build = tmp_path / "out"

    def run(*command: str) -> str:
        result = subprocess.run(command, capture_output=True, text=True, env=compiler_environment, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout + result.stderr

    run(cmake, "-S", str(tmp_path), "-B", str(build), "-G", "Ninja", f"-DCMAKE_C_COMPILER={CLANG}",
        f"-DCMAKE_OBJC_COMPILER={CLANG}", f"-DCMAKE_CXX_COMPILER={clangxx}",
        f"-DASCIICHAT_DEFER_NATIVE_BINARY={binary}", f"-DASCIICHAT_DEFER_BACKEND={backend}",
        f"-DASCIICHAT_LIBCLANG_PATH={compiler_environment['LIBCLANG_LIBRARY_FILE']}")
    assert "Transforming defer source batch" not in run(cmake, "--build", str(build))
    if header_mode:
        header.write_text('#include <ascii-chat/tooling/defer/defer.h>\n'
                          'static inline void clean(int *v) { defer(*v = 8); }\n', encoding="utf-8")
    else:
        source.write_text(
            '#include <ascii-chat/tooling/defer/defer.h>\n'
            'static int v; static int f(void) { defer(v = 8); return 1; }\n'
            'int main(void) { return f() != 1 || v != 8; }\n', encoding="utf-8"
        )
    output = run(cmake, "--build", str(build))
    assert "Transforming defer source batch" in output
    run(str(build / "src" / "ascii-chat-probe"))
    assert "Transforming defer source batch" not in run(cmake, "--build", str(build))


def test_objcpp_tool_accepts_exception_cleanup(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    compiler = shutil.which("clang++")
    if not compiler:
        pytest.skip("Clang C++ compiler is required")
    source = tmp_path / "source.mm"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        'int value; void f() { defer(value = 1); @throw (id)0; }\n', encoding="utf-8"
    )
    output = tmp_path / "output.mm"
    flags = ["-x", "objective-c++", "-std=c++11", "-fobjc-runtime=gnustep-2.0", "-fobjc-exceptions", "-fexceptions", "-I", str(ROOT / "include")]
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", *flags],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run([compiler, *flags, "-fsyntax-only", str(output)], capture_output=True, text=True,
                            env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr


def test_objcpp_tool_rejects_legacy_exception_runtime(tmp_path: Path, transform_environment: dict[str, str]) -> None:
    source = tmp_path / "source.mm"
    source.write_text('#include <ascii-chat/tooling/defer/defer.h>\nvoid f() { defer((void)0); }\n', encoding="utf-8")
    output = tmp_path / "output.mm"
    result = subprocess.run(
        [*tool_command(transform_environment), str(source), str(output), "--", "-x", "objective-c++", "-std=c++11",
         "-fobjc-runtime=gcc", "-I", str(ROOT / "include")],
        capture_output=True, text=True, env=transform_environment, check=False,
    )
    assert result.returncode != 0
    assert "zero-cost exceptions" in result.stderr
    assert not output.exists()


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


@pytest.mark.parametrize("parameter", ["int", "unsigned long", "double", "const int", "int *", "int (*)(int)"])
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


def test_unnamed_record_parameter_fails_closed(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    result = transform(tmp_path, transform_environment, "struct S { int n; }; int f(struct S) { defer((void)0); return 7; }\n")
    assert result.returncode != 0
    assert "unnamed parameter type" in result.stderr
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


@pytest.mark.parametrize("assembly", [
    '__asm__ goto ("" : : : : out);',
    '__asm__ volatile ("");',
    'JUMP();',
])
def test_inline_assembly_in_defer_function_fails_closed(
    tmp_path: Path, transform_environment: dict[str, str], assembly: str
) -> None:
    source = '#define JUMP() __asm__ goto ("" : : : : out)\n'
    source += f'void f(void) {{ defer((void)0); {assembly} out:; }}\n'
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode != 0
    assert "inline assembly in a function with defer" in result.stderr
    assert "input.c:" in result.stderr.replace("\\", "/")
    assert not (tmp_path / "output.c").exists()


def test_inline_assembly_in_unrelated_function_is_allowed(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = 'void a(void) { __asm__ volatile (""); }\nvoid b(void) { defer((void)0); }\n'
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr


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
                if mode != 3:
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
#define defer_named(name, ...) do {{}} while (0)
#define defer_cancel(name) do {{}} while (0)
static int trace[64], count;
static void mark(int value) {{ trace[count++] = value; }}
static void f(int mode) {{
  int i = 0;
  defer(mark({outer} + i));
  for (; i < 3; ++i) {{
    defer_named(iteration_cleanup, mark({loop} + i));
    if (i == 1) {{
      defer(mark({branch} + i));
      if (mode == 3) defer_cancel(iteration_cleanup);
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


@pytest.mark.parametrize("source", ["void f(void) { defer_named(h, (void)0); }", "void f(void) { defer_cancel(h); }", "void f(void) { USER_DEFER_WRAPPER(p); }"])
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


@pytest.mark.parametrize("allocator", ["malloc", "calloc"])
def test_deferred_safe_allocation_frees_on_early_return(
    tmp_path: Path, transform_environment: dict[str, str], allocator: str
) -> None:
    allocation = ("SAFE_MALLOC_DEFER_FREE(p, sizeof(int) * 2, int *)" if allocator == "malloc"
                  else "SAFE_CALLOC_DEFER_FREE(p, 2, sizeof(int), int *)")
    source = (
        'typedef __SIZE_TYPE__ size_t; extern void *malloc(size_t); extern void *calloc(size_t, size_t); '
        'extern void free(void *);\n#define NULL ((void *)0)\n'
        'static int releases;\n'
        '#define SAFE_MALLOC(size, cast) ((cast)malloc(size))\n'
        '#define SAFE_CALLOC(count, size, cast) ((cast)calloc(count, size))\n'
        '#define SAFE_FREE(p) do { if (p) { ++releases; free(p); p = NULL; } } while (0)\n'
        '#define SAFE_MALLOC_DEFER_FREE(name, size, cast) cast name = SAFE_MALLOC(size, cast); defer(SAFE_FREE(name))\n'
        '#define SAFE_CALLOC_DEFER_FREE(name, count, size, cast) cast name = SAFE_CALLOC(count, size, cast); defer(SAFE_FREE(name))\n'
        'static int *transferred;\n'
        f'static int f(int early) {{ {allocation}; if (!p) return 1; if (early == 1) return 0; '
        'if (early == 2) { transferred = p; p = NULL; return 0; } p[0] = 7; return p[0] != 7; }\n'
        'int main(void) { if (f(0) || f(1) || f(2)) return 1; free(transferred); return releases != 2; }\n'
    )
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_recursively_wrapped_defer_runs_on_return(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = (
        '#define INNER(action) defer(action)\n'
        '#define MIDDLE(name) int name = 7; INNER(total += name)\n'
        '#define OUTER(name) MIDDLE(name)\n'
        'static int total; static int f(void) { OUTER(value); return value; }\n'
        'int main(void) { return f() != 7 || total != 7; }\n'
    )
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_recursively_wrapped_variadic_defer(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = (
        '#define INNER(...) defer(__VA_ARGS__)\n'
        '#define OUTER(...) INNER(__VA_ARGS__)\n'
        'static int total; static void add(int a, int b) { total += a + b; }\n'
        'static void f(void) { OUTER(add(2, 3)); }\n'
        'int main(void) { f(); return total != 5; }\n'
    )
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_nested_wrapper_invocations_preserve_cleanup_order(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = (
        '#define INNER() defer(total = total * 10 + 1)\n'
        '#define OUTER(first) first; defer(total = total * 10 + 2)\n'
        'static int total; static void f(void) { OUTER(INNER()); }\n'
        'int main(void) { f(); return total != 21; }\n'
    )
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_object_like_wrapper_chain(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = (
        '#define INNER defer(total++)\n#define OUTER INNER\n'
        'static int total; static void f(void) { OUTER; }\n'
        'int main(void) { f(); return total != 1; }\n'
    )
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_wrapped_defer_with_definition_comments(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = (
        '#define WRAP(action) defer(/* block comment */ action) // trailing comment\n'
        'static int total; static void f(void) { WRAP(total++); total += 2; }\n'
        'int main(void) { f(); return total != 3; }\n'
    )
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_wrapped_defer_preserves_call_site_line(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = (
        '#define WRAP() int line = __LINE__; defer(total = line)\n'
        'static int total;\n'
        'static void f(void) { WRAP(); }\n'
        'int main(void) { f(); return total != 4; }\n'
    )
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode == 0, result.stderr
    executable = tmp_path / ("program.exe" if sys.platform == "win32" else "program")
    result = subprocess.run([str(CLANG), "-std=c17", str(tmp_path / "output.c"), "-o", str(executable)],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(executable)], check=False).returncode == 0


def test_invalid_wrapped_cleanup_reports_call_site(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = '#define WRAPPER() defer(missing_name++)\nvoid f(void) { WRAPPER(); }\n'
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode != 0
    assert "input.c:3:" in result.stderr.replace("\\", "/")
    assert "missing_name" in result.stderr
    assert not (tmp_path / "output.c").exists()


@pytest.mark.parametrize("source", [
    '#define WRAP(x) defer((void)0); (void)#x\nvoid f(void) { WRAP(a); }\n',
    '#define WRAP(x) defer((void)(x))\nvoid f(void) { WRAP(\n1); }\n',
    '#define VALUE 1\n#define STR(x) #x\n#define WRAP(x) const char *s = STR(x); defer((void)s)\nvoid f(void) { WRAP(VALUE); }\n',
    '#define WRAP(x) int a = x, b = x; defer((void)a); (void)b\nvoid f(void) { WRAP(__COUNTER__); }\n',
    '#define WRAP() int a = __COUNTER__; defer((void)a)\nvoid f(void) { WRAP(); }\n',
])
def test_unsafe_defer_wrappers_fail_closed(
    tmp_path: Path, transform_environment: dict[str, str], source: str
) -> None:
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode != 0
    assert "defer wrapper" in result.stderr
    assert "input.c:" in result.stderr.replace("\\", "/")
    assert not (tmp_path / "output.c").exists()


def test_token_pasted_hidden_defer_fails_closed(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = tmp_path / "input.c"
    output = tmp_path / "output.c"
    header = (ROOT / "include/ascii-chat/tooling/defer/defer.h").as_posix()
    source.write_text(
        f'#include "{header}"\n'
        '#define JOIN(a, b) a ## b\n#define WRAP(action) JOIN(de, fer)(action)\n'
        'void f(void) { WRAP((void)0); }\n', encoding="utf-8",
    )
    result = subprocess.run([*tool_command(transform_environment), str(source), str(output), "--", "-std=c17"],
                            capture_output=True, text=True, env=transform_environment, check=False)
    assert result.returncode != 0
    assert "defer" in result.stderr
    assert not output.exists()


@pytest.mark.parametrize("call", ["SAFE_MALLOC_DEFER_FREE(p, 4, char *)", "SAFE_CALLOC_DEFER_FREE(p, 1, 4, char *)"])
def test_untransformed_deferred_allocation_fails_compilation(
    tmp_path: Path, compiler_environment: dict[str, str], call: str
) -> None:
    source = tmp_path / "untransformed.c"
    source.write_text(
        '#define SAFE_MALLOC(size, cast) ((cast)0)\n'
        '#define SAFE_CALLOC(count, size, cast) ((cast)0)\n'
        '#include "ascii-chat/tooling/defer/defer.h"\n'
        '#define SAFE_FREE(p) ((void)(p))\n'
        '#define SAFE_MALLOC_DEFER_FREE(name, size, cast) cast name = SAFE_MALLOC(size, cast); defer(SAFE_FREE(name))\n'
        '#define SAFE_CALLOC_DEFER_FREE(name, count, size, cast) cast name = SAFE_CALLOC(count, size, cast); defer(SAFE_FREE(name))\n'
        f'void f(void) {{ {call}; (void)p; }}\n', encoding="utf-8",
    )
    result = subprocess.run(
        [str(CLANG), "-std=c17", "-fsyntax-only", f"-I{ROOT / 'include'}", str(source)],
        capture_output=True, text=True, env=compiler_environment, check=False,
    )
    assert result.returncode != 0
    assert "requires the source transformation" in result.stderr


def test_deferred_allocation_rejects_shadowed_pointer(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    source = (
        'typedef __SIZE_TYPE__ size_t; extern void *malloc(size_t); extern void free(void *);\n'
        '#define NULL ((void *)0)\n'
        '#define SAFE_MALLOC(size, cast) ((cast)malloc(size))\n'
        '#define SAFE_FREE(p) do { free(p); (p) = NULL; } while (0)\n'
        '#define SAFE_MALLOC_DEFER_FREE(name, size, cast) cast name = SAFE_MALLOC(size, cast); defer(SAFE_FREE(name))\n'
        'void f(void) { SAFE_MALLOC_DEFER_FREE(p, 4, char *); { char *p = NULL; (void)p; return; } }\n'
    )
    result = transform(tmp_path, transform_environment, source)
    assert result.returncode != 0
    assert "shadowed" in result.stderr
    assert not (tmp_path / "output.c").exists()


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


def test_defer_in_included_header_fails_before_writing(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    (tmp_path / "inline.h").write_text(
        "static inline void f(void) { defer((void)0); }\n", encoding="utf-8"
    )
    result = transform(tmp_path, transform_environment, '#include "inline.h"\nint main(void) { return 0; }\n')
    assert result.returncode != 0
    assert "inline.h:1:" in result.stderr.replace("\\", "/")
    assert "defer in included files is not supported" in result.stderr
    assert not (tmp_path / "output.c").exists()


@pytest.mark.parametrize("extension", ["c", "cpp", "m", "mm"])
def test_defer_in_included_source_fails_before_writing(
    tmp_path: Path, transform_environment: dict[str, str], extension: str
) -> None:
    included = tmp_path / f"included.{extension}"
    included.write_text("static void f(void) { defer((void)0); }\n", encoding="utf-8")
    result = transform(tmp_path, transform_environment, f'#include "{included.name}"\nint main(void) {{ return 0; }}\n')
    assert result.returncode != 0
    assert "defer in included files is not supported" in result.stderr
    assert not (tmp_path / "output.c").exists()


def test_wrapped_defer_in_included_header_fails_before_writing(
    tmp_path: Path, transform_environment: dict[str, str]
) -> None:
    (tmp_path / "inline.h").write_text(
        "#define WRAP(action) defer(action)\nstatic inline void f(void) { WRAP((void)0); }\n", encoding="utf-8"
    )
    result = transform(tmp_path, transform_environment, '#include "inline.h"\nint main(void) { return 0; }\n')
    assert result.returncode != 0
    assert "defer wrapper in included files is not supported" in result.stderr
    assert not (tmp_path / "output.c").exists()


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
    shutil.copy2(TOOL.parent / "analysis.py", tool_dir / "analysis.py")
    shutil.copy2(TOOL.parent / "manifest.py", tool_dir / "manifest.py")
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
    (source_dir / "allocated.c").write_text(
        '#include <stdlib.h>\n'
        '#define SAFE_MALLOC(size, cast) ((cast)malloc(size))\n'
        '#define SAFE_CALLOC(count, size, cast) ((cast)calloc(count, size))\n'
        '#define SAFE_FREE(p) do { free(p); (p) = NULL; } while (0)\n'
        '#include "ascii-chat/tooling/defer/defer.h"\n'
        '#define SAFE_MALLOC_DEFER_FREE(name, size, cast) cast name = SAFE_MALLOC(size, cast); defer(SAFE_FREE(name))\n'
        '#define SAFE_CALLOC_DEFER_FREE(name, count, size, cast) cast name = SAFE_CALLOC(count, size, cast); defer(SAFE_FREE(name))\n'
        'int allocated(void) { SAFE_MALLOC_DEFER_FREE(p, 4, char *); '
        'SAFE_CALLOC_DEFER_FREE(q, 2, 4, char *); return p == NULL || q == NULL; }\n',
        encoding="utf-8",
    )
    (source_dir / "wrappers.h").write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n'
        '#define INNER_WRAPPER(action) defer(action)\n'
        '#define OUTER_WRAPPER(action) INNER_WRAPPER(action)\n'
        '#define OBJECT_INNER defer((void)0)\n#define OBJECT_OUTER OBJECT_INNER\n', encoding="utf-8",
    )
    (source_dir / "wrapped.c").write_text(
        '#include "wrappers.h"\nvoid wrapped(void) { OUTER_WRAPPER((void)0); }\n', encoding="utf-8",
    )
    (source_dir / "object.c").write_text(
        '#include "wrappers.h"\nvoid object_wrapper(void) { OBJECT_OUTER; }\n', encoding="utf-8",
    )
    external = tmp_path / "external"
    external.mkdir()
    (external / "external_wrapper.h").write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n'
        '#define EXTERNAL_INNER(action) defer(action)\n'
        '#define EXTERNAL_OUTER(action) EXTERNAL_INNER(action)\n', encoding="utf-8",
    )
    (source_dir / "external.c").write_text(
        '#include "external_wrapper.h"\nvoid external_wrapper(void) { EXTERNAL_OUTER((void)0); }\n',
        encoding="utf-8",
    )
    late_header = source_dir / "late.h"
    late_header.write_text('#include "ascii-chat/tooling/defer/defer.h"\n#define LATE(action) ((void)0)\n', encoding="utf-8")
    (source_dir / "late.c").write_text('#include "late.h"\nvoid late(void) { LATE((void)0); }\n', encoding="utf-8")
    main = source_dir / "main.c"
    main.write_text("int main(void) { return 0; }\n", encoding="utf-8")
    header = source_dir / "value.h"
    header.write_text("#define VALUE 1\n", encoding="utf-8")
    nested = source_dir / "nested"
    nested.mkdir()
    (nested / "value_wrapper.h").write_text('#include "../value.h"\n', encoding="utf-8")
    (source_dir / "included.c").write_text(
        '#include "nested/value_wrapper.h"\nstatic int included_value(void) { return VALUE; }\n',
        encoding="utf-8",
    )
    for extension in ("cpp", "m", "mm"):
        (source_dir / f"included.{extension}").write_text(
            f"static int included_{extension}(void) {{ return 0; }}\n", encoding="utf-8"
        )
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
        "add_executable(ascii-chat-probe main.c second.c named.c allocated.c wrapped.c object.c external.c late.c)\n"
        "set_source_files_properties(second.c PROPERTIES COMPILE_DEFINITIONS SECOND_REQUIRED)\n"
        "set_target_properties(ascii-chat-probe PROPERTIES UNITY_BUILD ON)\n"
        "add_dependencies(ascii-chat-probe generate_version)\n"
        f'target_include_directories(ascii-chat-probe PRIVATE "{include_dir}" "{external.as_posix()}" "${{CMAKE_BINARY_DIR}}")\n',
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
        f"-DASCIICHAT_DEFER_WRAPPER_HEADER_DIRS={external}",
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
    build_rules = (build / "build.ninja").read_text(encoding="utf-8")
    defer_commands = [line for line in build_rules.splitlines() if "--batch" in line and "defer" in line]
    assert defer_commands
    assert all("--stdin" not in line for line in defer_commands)
    second_objects = list(build.rglob("*second.c.obj")) + list(build.rglob("*second.c.o"))
    assert len(second_objects) == 1
    second_object = second_objects[0]
    second_mtime = second_object.stat().st_mtime_ns
    if backend == "PYTHON":
        time.sleep(1.1)
        analysis_module = tool_dir / "analysis.py"
        analysis_module.write_text(analysis_module.read_text(encoding="utf-8") + "\n# analysis update\n", encoding="utf-8")
        output = run(cmake, "--build", str(build))
        assert "Transforming defer source batch" in output
        assert second_object.stat().st_mtime_ns == second_mtime
    time.sleep(1.1)
    main.write_text("int main(void) { return 0; } /* ordinary edit */\n", encoding="utf-8")
    output = run(cmake, "--build", str(build))
    assert "Re-running CMake" not in output
    assert "Configuring done" not in output
    assert "Transforming defer source batch" not in output
    assert "Building C object" in output
    assert second_object.stat().st_mtime_ns == second_mtime

    time.sleep(1.1)
    main.write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n'
        'static int cleaned; static void f(void) { defer(cleaned = 1); }\n'
        'int main(void) { f(); return cleaned != 1; }\n', encoding="utf-8"
    )
    output = run(cmake, "--build", str(build))
    assert "Re-running CMake" in output
    assert "Transforming defer source batch" in output
    executable = build / "src" / ("ascii-chat-probe.exe" if sys.platform == "win32" else "ascii-chat-probe")
    run(str(executable))
    assert second_object.stat().st_mtime_ns == second_mtime
    generated_main = build / "defer_transformed" / "ascii-chat-probe" / "src" / "main.c.defer" / "main.c"
    assert generated_main.is_file()
    main_objects = list(build.rglob("*main.c.obj")) + list(build.rglob("*main.c.o"))
    assert len(main_objects) == 1
    main_object = main_objects[0]
    main_mtime = main_object.stat().st_mtime_ns
    time.sleep(1.1)
    main.write_text(main.read_text(encoding="utf-8") + "/* transformed edit */\n", encoding="utf-8")
    output = run(cmake, "--build", str(build))
    assert "Re-running CMake" not in output
    assert "Configuring done" not in output
    assert "Transforming defer source batch" in output
    assert second_object.stat().st_mtime_ns == second_mtime
    assert main_object.stat().st_mtime_ns > main_mtime
    if backend != "PYTHON":
        assert "defer-tool: transformed 1 of " in output
    run(str(executable))
    output = run(cmake, "--build", str(build))
    assert "Re-running CMake" not in output
    assert "Transforming defer source batch" not in output
    assert second_object.stat().st_mtime_ns == second_mtime
    if backend != "PYTHON":
        database = build / "compile_commands.json"
        time.sleep(1.1)
        database.write_text(database.read_text(encoding="utf-8") + "\n", encoding="utf-8")
        output = run(cmake, "--build", str(build))
        assert "defer-tool: transformed 0 of " in output
        assert second_object.stat().st_mtime_ns == second_mtime
    time.sleep(1.1)
    main.write_text("int main(void) { return 0; }\n", encoding="utf-8")
    output = run(cmake, "--build", str(build))
    assert "Re-running CMake" in output
    assert second_object.stat().st_mtime_ns == second_mtime
    run(str(executable))
    time.sleep(1.1)
    late_header.write_text('#include "ascii-chat/tooling/defer/defer.h"\n#define LATE(action) defer(action)\n', encoding="utf-8")
    main.write_text(
        '#include "ascii-chat/tooling/defer/defer.h"\n#include "included.c"\n'
        '#include "included.cpp"\n#include "included.m"\n#include "included.mm"\n#include "ready.h"\n'
        "static int n; static void f(void) { defer\n (n = VALUE); }\n"
        "int main(void) { f(); return n != included_value() || included_cpp() || included_m() || included_mm(); }\n",
        encoding="utf-8",
    )
    collision = build / "defer_transformed" / "ascii-chat-probe" / "src" / "included.c"
    collision.parent.mkdir(parents=True, exist_ok=True)
    collision.write_text('#error generated-directory include shadowed the source file\n', encoding="utf-8")
    (build / "ready.h").unlink()
    output = run(cmake, "--build", str(build))
    assert output.count("Transforming defer source batch") == 1
    assert (
        build / "defer_transformed" / "ascii-chat-probe" / "src" / "second.c.defer" / "second.c"
    ).is_file()
    assert (build / "defer_transformed" / "ascii-chat-probe" / "src" / "late.c.defer" / "late.c").is_file()
    generated = build / "defer_transformed" / "ascii-chat-probe" / "src" / "main.c.defer" / "main.c"
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
        build / "defer_transformed" / "ascii-chat-probe" / "src" / "second.c.defer" / "second.c"
    )
    objects = list(
        (build / "src" / "CMakeFiles" / "ascii-chat-probe.dir").rglob(
            "*.obj" if sys.platform == "win32" else "*.o"
        )
    )
    other_object = next(path for path in objects if "second.c" in path.name)
    main_object = next(path for path in objects if "main.c" in path.name)
    before = generated.stat().st_mtime_ns
    other_before = other_generated.stat().st_mtime_ns
    object_before = other_object.stat().st_mtime_ns
    main_object_before = main_object.stat().st_mtime_ns
    # Some filesystems have coarse timestamp resolution.
    time.sleep(1.1)
    header.write_text("#define VALUE 2\n", encoding="utf-8")
    run(cmake, "--build", str(build))
    assert generated.stat().st_mtime_ns == before
    assert other_generated.stat().st_mtime_ns == other_before
    assert other_object.stat().st_mtime_ns == object_before
    assert main_object.stat().st_mtime_ns > main_object_before
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
    original_main = main.read_text(encoding="utf-8")
    time.sleep(1.1)
    main.write_text(original_main + "\nvoid invalid(void) { defer(return); }\n", encoding="utf-8")
    failed = subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True, env=compiler_environment)
    assert failed.returncode != 0
    assert "defer-tool:" in failed.stdout + failed.stderr
    main.write_text(original_main, encoding="utf-8")
    run(cmake, "--build", str(build))
    run(str(executable))


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
