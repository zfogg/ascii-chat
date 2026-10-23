"""Run a Python CTest harness with consistent native-coverage handling."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import argparse
import os
from pathlib import Path
import subprocess
import sys


def append_lsan_suppression(environment):
    """Suppress interpreter-owned teardown allocations without masking native leaks."""
    if "suppressions=" in environment.get("LSAN_OPTIONS", ""):
        return
    suppression = Path(__file__).with_name("python_interpreter.lsan.supp")
    options = environment.get("LSAN_OPTIONS", "")
    environment["LSAN_OPTIONS"] = ":".join(filter(None, (options, f"suppressions={suppression}")))


def coverage_counters(root):
    return {path: path.stat().st_mtime_ns for path in root.rglob("*.gcda")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--coverage-build", action="store_true")
    parser.add_argument("--coverage-root", type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a Python test command is required")

    environment = dict(os.environ)
    append_lsan_suppression(environment)
    before = {}
    if args.coverage_build:
        if args.coverage_root is None:
            parser.error("--coverage-root is required with --coverage-build")
        root = args.coverage_root.resolve()
        # Redirected profiles are useful for a focused probe, but cannot be
        # merged by the build-tree gcovr invocation used by CI.
        environment.pop("GCOV_PREFIX", None)
        environment.pop("GCOV_PREFIX_STRIP", None)
        environment["ASCIICHAT_COVERAGE_BUILD"] = "1"
        environment["ASCIICHAT_COVERAGE_ROOT"] = str(root)
        before = coverage_counters(root)

    result = subprocess.run(command, env=environment)
    if args.coverage_build and result.returncode == 0:
        after = coverage_counters(root)
        updated = sum(path not in before or timestamp > before[path] for path, timestamp in after.items())
        print(f"Python CTest native coverage counters updated: {updated}")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
