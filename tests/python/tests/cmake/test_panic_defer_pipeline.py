"""Exercise panic instrumentation followed by defer transformation in CMake."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[4]


def test_panic_and_defer_build_and_rebuild(tmp_path: Path) -> None:
    panic = os.environ.get("ASCIICHAT_PANIC_TEST_BINARY")
    defer = os.environ.get("DEFER_TEST_BINARY")
    cmake = shutil.which("cmake")
    clang = shutil.which("clang")
    if not panic or not defer or not cmake or not clang or not shutil.which("ninja"):
        pytest.skip("panic and defer binaries, CMake, Clang and Ninja are required")
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
    source = source_dir / "main.c"
    source.write_text(
        '#include <recast/defer.h>\n'
        '#include <ascii-chat/tooling/panic/instrument_log.h>\n'
        'static int cleanup_count, log_count;\n'
        'ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER\n'
        'void asciichat_instr_log_line(const char *p, uint32_t l, const char *f, '
        'const char *s, uint8_t m) { (void)p; (void)l; (void)f; (void)s; (void)m; ++log_count; }\n'
        'static int work(void) { DEFER(++cleanup_count); return 7; }\n'
        'int main(void) { int result = work(); return result != 7 || cleanup_count != 1 || log_count == 0; }\n',
        encoding="utf-8",
    )
    (source_dir / "other.c").write_text("int other(void) { return 3; }\n", encoding="utf-8")
    (source_dir / "CMakeLists.txt").write_text(
        f'add_executable(ascii-chat main.c other.c)\ntarget_include_directories(ascii-chat PRIVATE "{(ROOT / "include").as_posix()}" "{(ROOT / "deps" / "recast" / "cpp" / "include").as_posix()}")\n',
        encoding="utf-8",
    )
    (project / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.30)\nproject(panic_defer C CXX)\n'
        'set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
        f'include("{(ROOT / "cmake" / "tooling" / "Panic.cmake").as_posix()}")\n'
        f'include("{(ROOT / "deps" / "recast" / "Recast.cmake").as_posix()}")\n'
        'ascii_panic_prepare()\nadd_subdirectory(src)\nset(DEFER_SOURCE_HOOK ascii_panic_defer_source)\n'
        'recast_targets(ascii-chat)\nascii_panic_finalize()\n',
        encoding="utf-8",
    )
    build = project / "build"
    configured = subprocess.run(
        [cmake, "-S", str(project), "-B", str(build), "-G", "Ninja", f"-DCMAKE_C_COMPILER={clang}",
         "-DASCIICHAT_BUILD_WITH_PANIC=ON", f"-DASCIICHAT_PANIC_TOOL={panic}",
         f"-DDEFER_NATIVE_BINARY={defer}"],
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
        instrumented_other = build / "panic_transformed" / "ascii-chat" / "src" / "other.c"
        assert "asciichat_instr_log_line" in instrumented_other.read_text(encoding="utf-8")

    # The transformer must not silently accept unsafe control flow.
    unsafe = original + '\nstatic void unsafe(void) { __asm__ volatile("nop"); DEFER((void)0); }\n'
    source.write_text(unsafe, encoding="utf-8")
    rejected = subprocess.run([cmake, "--build", str(build)], capture_output=True, text=True, check=False)
    assert rejected.returncode != 0
    assert "inline assembly" in rejected.stdout + rejected.stderr
    assert source.read_text(encoding="utf-8") == unsafe
