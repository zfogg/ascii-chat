"""Require complete line/branch coverage of the shared keyboard Unicode adapter.

The ordinary Criterion target runs with the project's sanitizers. This separate
measurement reports only our Unicode adapter functions, without sanitizer-generated branches
or vendored utf8proc internals. It runs the same Criterion source, not fixtures.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys


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
    for name in ("keyboard.gcda", "keyboard_text_test.gcda"):
        (output / name).unlink(missing_ok=True)
    flags = ["-g", "-O0", "-ffunction-sections", "-fdata-sections", "-DUTF8PROC_STATIC", "-I" + str(root / "include"), "-I" + str(root / "deps"), "-I" + str(build / "generated")]
    utf8proc = root / "deps/ascii-chat-deps/utf8proc/utf8proc.c"
    run([clang, *flags, "-c", utf8proc, "-o", "utf8proc.o"], output)
    criterion = shlex.split(run(["pkg-config", "--cflags", "criterion"], root))
    for source in (root / "lib/platform/keyboard.c", root / "tests/unit/platform/keyboard_text_test.c"):
        run([clang, *flags, *criterion, "--coverage", "-c", source, "-o", source.stem + ".o"], output)
    libraries = shlex.split(run(["pkg-config", "--libs", "criterion"], root))
    link_gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
    run([clang, "--coverage", link_gc, "keyboard.o", "keyboard_text_test.o", "utf8proc.o",
         *libraries, "-o", "keyboard-text-test"], output)
    tests = run([output / "keyboard-text-test", "--jobs", "1"], output)
    print(tests, end="")
    report = run([llvm_cov, "gcov", "-b", "-c", "-o", "keyboard.gcno",
                  root / "lib/platform/keyboard.c"], output)
    print(report, end="")
    functions = {"keyboard_text_decode", "keyboard_text_encode", "keyboard_text_utf16"}
    listing = (output / "keyboard.c.gcov").read_text(encoding="utf-8")
    selected = []
    active = False
    found = set()
    for line in listing.splitlines():
        function = re.match(r"function (\w+) called", line)
        if function:
            active = function[1] in functions
            if active:
                found.add(function[1])
        if active:
            selected.append(line)
    if found != functions:
        raise RuntimeError("Missing Unicode function counters")
    counters = []
    branches = []
    for line in selected:
        count = re.match(r"\s*([^:]+):\s*\d+:", line)
        if count and count[1].strip() != "-":
            counters.append(count[1].strip().replace(",", ""))
        branch = re.match(r"branch\s+\d+ taken (\d+)", line)
        if branch:
            branches.append(int(branch[1]))
        elif line.startswith("branch "):
            branches.append(0)
    if not counters or not branches:
        raise RuntimeError("Missing execution counters")
    covered = sum(value.rstrip("*").isdigit() and int(value.rstrip("*")) > 0 for value in counters)
    summary = {"source": "lib/platform/keyboard.c", "functions": sorted(functions),
               "line_percent": 100 * covered / len(counters),
               "branch_percent": 100 * sum(value > 0 for value in branches) / len(branches),
               "lines": len(counters), "branches": len(branches)}
    (output / "unicode.gcov").write_text("\n".join(selected) + "\n", encoding="utf-8")
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    (output / "summary.txt").write_text(tests + report + json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    if summary["line_percent"] != 100 or summary["branch_percent"] != 100:
        raise RuntimeError("Keyboard Unicode adapter requires 100% line and branch coverage")


if __name__ == "__main__":
    main()
