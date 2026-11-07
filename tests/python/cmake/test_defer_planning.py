"""Unit tests for defer source discovery and build-batch planning."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]
TOOL_DIR = ROOT / "src" / "tooling" / "defer"


def run_scanner(paths: list[Path]) -> list[str]:
    result = subprocess.run(
        [sys.executable, str(TOOL_DIR / "scan.py")],
        input="".join(f"{path.as_posix()}\n" for path in paths).encode("utf-8"),
        capture_output=True,
        check=True,
    )
    return result.stdout.decode("utf-8").splitlines()


@pytest.mark.parametrize(
    ("source", "expected"),
    [
        (b"void f(void) { defer(cleanup()); }", True),
        (b"void f(void) { defer /* cleanup (LIFO) */ (cleanup()); }", True),
        (b"void f(void) { defer // cleanup\n (cleanup()); }", True),
        (b"void f(void) { defer \\\n (cleanup()); }", True),
        (b"void f(void) { not_defer(cleanup()); }", False),
        (b"void f(void) { deferred(cleanup()); }", False),
        (b"void f(void) {}", False),
    ],
)
def test_scanner_recognizes_defer_spelling(
    tmp_path: Path, source: bytes, expected: bool
) -> None:
    path = tmp_path / "cleanup.c"
    path.write_bytes(source)
    assert run_scanner([path]) == ([path.as_posix()] if expected else [])


def test_scanner_preserves_unicode_paths_and_input_order(tmp_path: Path) -> None:
    first = tmp_path / "source-é.c"
    second = tmp_path / "source-猫.c"
    ignored = tmp_path / "plain.c"
    for path in (first, second):
        path.write_text("void f(void) { defer(cleanup()); }\n", encoding="utf-8")
    ignored.write_text("void f(void) {}\n", encoding="utf-8")
    assert run_scanner([second, ignored, first]) == [
        second.as_posix(),
        first.as_posix(),
    ]
    assert run_scanner([]) == []


def run_planner(records: Path, build: Path, stage: str, output: Path) -> None:
    subprocess.run(
        [
            sys.executable,
            str(TOOL_DIR / "plan.py"),
            "--records",
            str(records),
            "--build-dir",
            str(build),
            "--stage",
            stage,
            "--output",
            str(output),
        ],
        capture_output=True,
        check=True,
    )


def test_planner_groups_matching_prerequisites_and_separates_stages(
    tmp_path: Path,
) -> None:
    records = tmp_path / "records.txt"
    records.write_text(
        "core\t/src/a.c\t/out/a.c\tversion;headers\n"
        "crypto\t/src/b.c\t/out/b.c\theaders;version\n"
        "other\t/src/c.c\t/out/c.c\tversion\n",
        encoding="utf-8",
    )
    build = tmp_path / "build"
    plan = build / "plan.cmake"
    run_planner(records, build, "production", plan)
    manifests = sorted((build / "defer_transformed").glob("batch-*.txt"))
    assert len(manifests) == 2
    assert sorted(path.read_text(encoding="utf-8") for path in manifests) == sorted(
        ["/src/a.c\t/out/a.c\n/src/b.c\t/out/b.c\n", "/src/c.c\t/out/c.c\n"]
    )
    rules = plan.read_text(encoding="utf-8")
    assert rules.count("add_custom_command(") == 2
    assert "[=[core]=]" in rules and "[=[crypto]=]" in rules
    assert "[=[headers]=]" in rules and "[=[version]=]" in rules

    before = {path: path.stat().st_mtime_ns for path in [plan, *manifests]}
    run_planner(records, build, "production", plan)
    assert {path: path.stat().st_mtime_ns for path in before} == before

    run_planner(records, build, "tests", build / "tests.cmake")
    assert len(list((build / "defer_transformed").glob("batch-*.txt"))) == 4


def test_planner_handles_empty_input_and_cmake_delimiters(tmp_path: Path) -> None:
    records = tmp_path / "records.txt"
    records.write_text("", encoding="utf-8")
    build = tmp_path / "build"
    output = build / "plan.cmake"
    run_planner(records, build, "production", output)
    assert output.read_text(encoding="utf-8") == ""

    records.write_text("target\t/src/a]=]b.c\t/out/a]=]b.c\t\n", encoding="utf-8")
    run_planner(records, build, "production", output)
    rules = output.read_text(encoding="utf-8")
    assert "[==[/src/a]=]b.c]==]" in rules
    assert "[==[/out/a]=]b.c]==]" in rules
