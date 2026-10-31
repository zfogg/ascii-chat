"""Require complete line/branch coverage of the shared keyboard Unicode adapter.

The ordinary Criterion target runs with the project's sanitizers. This separate
measurement instruments only our adapter, without sanitizer-generated branches
or vendored utf8proc internals. It runs the same Criterion source, not fixtures.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess


def run(command, cwd):
    result = subprocess.run([str(arg) for arg in command], cwd=cwd, check=True,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = args.build.resolve()
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    compiler = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$", cache, re.MULTILINE)
    if not compiler:
        raise RuntimeError("Configure a Clang build before measuring coverage")
    clang = Path(compiler.group(1).strip())
    llvm_cov = clang.with_name("llvm-cov")
    if not llvm_cov.exists():
        llvm_cov = shutil.which("llvm-cov")
    if not llvm_cov:
        raise RuntimeError("llvm-cov is required")
    clang_major = re.search(r"clang version (\d+)", run([clang, "--version"], root))
    cov_major = re.search(r"version (\d+)", run([llvm_cov, "--version"], root))
    if not clang_major or not cov_major or clang_major[1] != cov_major[1]:
        raise RuntimeError("Clang and llvm-cov versions must match")
    # Keep this CFG separate from the sanitizer-instrumented counters that the
    # normal gcovr report merges from the configured build directory.
    output = build.with_name(build.name + "-keyboard-text-coverage")
    output.mkdir(parents=True, exist_ok=True)
    for name in ("keyboard_text.gcda", "keyboard_text_test.gcda"):
        (output / name).unlink(missing_ok=True)
    flags = ["-g", "-O0", "-DUTF8PROC_STATIC", "-I" + str(root / "include"), "-I" + str(root / "deps")]
    utf8proc = root / "deps/ascii-chat-deps/utf8proc/utf8proc.c"
    run([clang, *flags, "-c", utf8proc, "-o", "utf8proc.o"], output)
    criterion = shlex.split(run(["pkg-config", "--cflags", "criterion"], root))
    for source in (root / "lib/platform/keyboard_text.c", root / "tests/unit/platform/keyboard_text_test.c"):
        run([clang, *flags, *criterion, "--coverage", "-c", source, "-o", source.stem + ".o"], output)
    libraries = shlex.split(run(["pkg-config", "--libs", "criterion"], root))
    run([clang, "--coverage", "keyboard_text.o", "keyboard_text_test.o", "utf8proc.o",
         *libraries, "-o", "keyboard-text-test"], output)
    tests = run([output / "keyboard-text-test", "--jobs", "1"], output)
    print(tests, end="")
    report = run([llvm_cov, "gcov", "-b", "-c", "-o", "keyboard_text.gcno",
                  root / "lib/platform/keyboard_text.c"], output)
    print(report, end="")
    (output / "summary.txt").write_text(tests + report, encoding="utf-8")
    lines = re.search(r"Lines executed:([0-9.]+)% of ([0-9]+)", report)
    branches = re.search(r"Taken at least once:([0-9.]+)% of ([0-9]+)", report)
    if not lines or not branches or not int(lines[2]) or not int(branches[2]):
        raise RuntimeError("Missing execution counters")
    summary = {"source": "lib/platform/keyboard_text.c", "line_percent": float(lines[1]),
               "branch_percent": float(branches[1]), "lines": int(lines[2]), "branches": int(branches[2])}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    listing = (output / "keyboard_text.c.gcov").read_text(encoding="utf-8")
    missed = re.search(r"^\s*#####:", listing, re.MULTILINE) or re.search(
        r"^branch\s+\d+ taken 0(?:\s|$)", listing, re.MULTILINE)
    if missed or summary["line_percent"] != 100 or summary["branch_percent"] != 100:
        raise RuntimeError("Keyboard Unicode adapter requires 100% line and branch coverage")


if __name__ == "__main__":
    main()
