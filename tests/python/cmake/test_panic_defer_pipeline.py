"""Exercise panic instrumentation followed by defer transformation in CMake."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[3]


@pytest.mark.parametrize("backend", ["NATIVE", "PYTHON"])
def test_panic_and_defer_build_and_rebuild(tmp_path: Path, backend: str) -> None:
    panic = os.environ.get("ASCIICHAT_PANIC_TEST_BINARY")
    defer = os.environ.get("ASCIICHAT_DEFER_TEST_BINARY")
    cmake = shutil.which("cmake")
    clang = shutil.which("clang")
    if not panic or not defer or not cmake or not clang or not shutil.which("ninja"):
        pytest.skip("panic and defer binaries, CMake, Clang and Ninja are required")
    if backend == "PYTHON" and not shutil.which("uv"):
        pytest.skip("the Python defer backend requires uv")

    project = tmp_path / "project"
    source_dir = project / "src"
    source_dir.mkdir(parents=True)
    utility_dir = project / "cmake" / "utils"
    utility_dir.mkdir(parents=True)
    shutil.copy2(ROOT / "cmake" / "utils" / "BuildLLVMTool.cmake", utility_dir)
    panic_source = source_dir / "tooling" / "panic"
    panic_source.mkdir(parents=True)
    for name in ("tool.cpp", "CMakeLists.txt"):
        shutil.copy2(ROOT / "src" / "tooling" / "panic" / name, panic_source)
    if backend == "PYTHON":
        python_source = source_dir / "tooling" / "defer"
        python_source.mkdir()
        for name in ("defer.py", "plan.py", "scan.py", "pyproject.toml", "uv.lock"):
            shutil.copy2(ROOT / "src" / "tooling" / "defer" / name, python_source)
        shutil.copytree(ROOT / "src" / "tooling" / "defer" / "vendor", python_source / "vendor")
    source = source_dir / "main.c"
    source.write_text(
        '#include <ascii-chat/tooling/defer/defer.h>\n'
        '#include <ascii-chat/tooling/panic/instrument_log.h>\n'
        'static int cleanup_count, log_count;\n'
        'ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER\n'
        'void asciichat_instr_log_line(const char *p, uint32_t l, const char *f, '
        'const char *s, uint8_t m) { (void)p; (void)l; (void)f; (void)s; (void)m; ++log_count; }\n'
        'static int work(void) { defer(++cleanup_count); return 7; }\n'
        'int main(void) { int result = work(); return result != 7 || cleanup_count != 1 || log_count == 0; }\n',
        encoding="utf-8",
    )
    (source_dir / "CMakeLists.txt").write_text(
        f'add_executable(ascii-chat main.c)\ntarget_include_directories(ascii-chat PRIVATE "{(ROOT / "include").as_posix()}")\n',
        encoding="utf-8",
    )
    (project / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.30)\nproject(panic_defer C CXX)\n'
        'set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        f'include("{(ROOT / "cmake" / "tooling" / "Panic.cmake").as_posix()}")\n'
        f'include("{(ROOT / "cmake" / "tooling" / "Defer.cmake").as_posix()}")\n'
        'ascii_panic_prepare()\nadd_subdirectory(src)\nascii_defer_prepare()\nascii_panic_finalize()\n',
        encoding="utf-8",
    )
    build = project / "build"
    configured = subprocess.run(
        [cmake, "-S", str(project), "-B", str(build), "-G", "Ninja", f"-DCMAKE_C_COMPILER={clang}",
         "-DASCIICHAT_BUILD_WITH_PANIC=ON", f"-DASCIICHAT_DEFER_BACKEND={backend}", f"-DASCIICHAT_PANIC_TOOL={panic}",
         f"-DASCIICHAT_DEFER_NATIVE_BINARY={defer}"],
        capture_output=True, text=True, check=False,
    )
    assert configured.returncode == 0, configured.stdout + configured.stderr
    original = source.read_text(encoding="utf-8")
    for suffix in ("", "\n/* force regeneration */\n"):
        if suffix:
            source.write_text(original + suffix, encoding="utf-8")
        result = subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        executable = build / "src" / ("ascii-chat.exe" if os.name == "nt" else "ascii-chat")
        run = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
        assert run.returncode == 0, run.stderr
        assert source.read_text(encoding="utf-8") == original + suffix

    # Neither backend may silently accept an exit whose control flow is unsafe.
    unsafe = original + '\nstatic void unsafe(void) { __asm__ volatile("nop"); defer((void)0); }\n'
    source.write_text(unsafe, encoding="utf-8")
    rejected = subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True, check=False)
    assert rejected.returncode != 0
    assert "inline assembly" in rejected.stdout + rejected.stderr
    assert source.read_text(encoding="utf-8") == unsafe
