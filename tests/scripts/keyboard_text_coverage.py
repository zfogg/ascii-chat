"""Require complete line/branch coverage of the shared keyboard Unicode adapter."""
import argparse
import json
from pathlib import Path
import re
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
    executable = build / "bin" / ("unit_tests.exe" if sys.platform == "win32" else "unit_tests")
    if not executable.exists():
        raise RuntimeError(f"Build {executable} before measuring coverage")
    output = build / "keyboard-text-coverage"
    output.mkdir(parents=True, exist_ok=True)
    tests = run([executable, "*keyboard*"] , build)
    print(tests, end="")
    object_dir = build / "tests/CMakeFiles/unit_tests.dir/unit/platform"
    report = run([llvm_cov, "gcov", "-b", "-c", "-o", object_dir,
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
