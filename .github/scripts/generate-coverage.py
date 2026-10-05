"""Merge Clang gcov counters into one repository-relative Cobertura report."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


def find_llvm_cov(build):
    cache = (build / "CMakeCache.txt").read_text()
    match = re.search(r"^CMAKE_C_COMPILER:(?:FILEPATH|STRING)=(.+)$", cache, re.MULTILINE)
    if not match:
        raise ValueError("CMakeCache.txt does not identify the C compiler")
    compiler = Path(match.group(1)).resolve()
    version = subprocess.check_output([str(compiler), "--version"], text=True)
    major = re.search(r"clang version (\d+)", version)
    if not major:
        raise ValueError("Coverage requires the configured Clang compiler")
    candidates = [compiler.parent / "llvm-cov", shutil.which(f"llvm-cov-{major[1]}"), shutil.which("llvm-cov")]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            output = subprocess.check_output([str(candidate), "--version"], text=True)
            if re.search(rf"version {major[1]}\.", output):
                return str(candidate)
    raise ValueError(f"Cannot find llvm-cov matching Clang {major[1]}")


def validate_report(path):
    report = ET.parse(path).getroot()
    covered = int(report.get("lines-covered", "0"))
    valid = int(report.get("lines-valid", "0"))
    if covered <= 0 or valid <= 0:
        raise ValueError("Coverage report has no executed source lines")
    for entry in report.findall(".//class"):
        filename = entry.get("filename", "")
        if not filename or Path(filename).is_absolute() or ".." in Path(filename).parts:
            raise ValueError(f"Coverage filename is not repository-relative: {filename}")
    return covered, valid


def generate(root, build, output):
    counters = [path for path in build.rglob("*.gcda") if path.with_suffix(".gcno").is_file()]
    if not counters:
        raise ValueError("No matching gcda/gcno counters found in the build directory")
    llvm_cov = find_llvm_cov(build)
    gcovr = shutil.which("gcovr") or str(Path(sys.executable).with_name("gcovr"))
    if not Path(gcovr).is_file():
        raise ValueError("gcovr is not installed")
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        gcovr, "--root", str(root),
        "--gcov-executable", f"{llvm_cov} gcov", "--xml-pretty", "--output", str(output),
        "--filter", r"(lib|src|include|tests)/", "--exclude", r"deps/", "--exclude", r"\.deps-cache/",
        str(build),
    ], cwd=root, check=True)
    covered, valid = validate_report(output)
    print(f"Merged coverage: {covered}/{valid} executed lines ({100 * covered / valid:.2f}%)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--output", type=Path, default=Path("build/coverage.xml"))
    args = parser.parse_args()
    try:
        generate(args.root.resolve(), args.build.resolve(), args.output.resolve())
    except (ValueError, OSError, subprocess.CalledProcessError, ET.ParseError) as error:
        parser.exit(1, f"Coverage generation failed: {error}\n")


if __name__ == "__main__":
    main()
