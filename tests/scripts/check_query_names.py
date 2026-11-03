"""Check production SQLite call sites using Gotel's Clang-based naming API.

Run in the pinned telemetry Python environment after configuring CMake. The
compile database supplies project configuration; SQL parsing and identity policy
belong to Gotel and Sequel.
"""

import argparse
import importlib
import json
import pathlib
import shlex
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CATALOG_SOURCE = ROOT / "lib/discovery/database.c"
EXECUTORS = (
    "ascii_otel_sqlite_prepare",
    "sqlite3_exec",
    "sqlite3_prepare_v2",
    "sqlite3_prepare_v3",
)
sys.path.insert(0, str(ROOT / "tests"))
importlib.import_module("_environment")


def analysis_arguments(command: dict[str, object]) -> tuple[str, ...]:
    raw_arguments = command.get("arguments")
    if isinstance(raw_arguments, list) and all(
        isinstance(item, str) for item in raw_arguments
    ):
        arguments = raw_arguments
    else:
        command_text = command.get("command")
        if not isinstance(command_text, str):
            raise ValueError("compile database entry must contain command or arguments")
        arguments = shlex.split(command_text)
    # CMake passes frontend PCH options through repeated -Xclang switches.
    # Reparse the source headers rather than depending on a compiler cache.
    arguments = [argument for argument in arguments if argument != "-Xclang"]
    result: list[str] = []
    paired = {
        "-I",
        "-isystem",
        "-iquote",
        "-include",
        "-include-pch",
        "-imacros",
        "-isysroot",
        "--sysroot",
        "-target",
    }
    cursor = 1
    while cursor < len(arguments):
        argument = arguments[cursor]
        if not isinstance(argument, str):
            raise TypeError("compile argument must be a string")
        if argument in paired:
            if cursor + 1 >= len(arguments) or not isinstance(
                arguments[cursor + 1], str
            ):
                raise ValueError("compile option is missing its value")
            value = arguments[cursor + 1]
            if argument != "-include-pch" and not (
                argument == "-include"
                and pathlib.Path(value).name.startswith("cmake_pch.")
            ):
                result.extend((argument, value))
            cursor += 2
        else:
            if argument.startswith(
                ("-D", "-U", "-I", "-std=", "--target=", "--sysroot=")
            ):
                result.append(argument)
            cursor += 1
    return tuple(result)


def main() -> None:
    from gotel.querynames import CConfig, CExecutor, check_c

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, default=ROOT / "build")
    parser.add_argument("--clang", default="clang")
    args = parser.parse_args()
    commands = json.loads((args.build_dir / "compile_commands.json").read_text())
    entry = next(
        command
        for command in commands
        if pathlib.Path(command["file"]).resolve() == CATALOG_SOURCE
    )
    # Inventory candidates only; Gotel's AST decides which names are actual
    # executor calls. Newly compiled storage sources join the check automatically.
    candidates = {CATALOG_SOURCE}
    for source in {pathlib.Path(command["file"]).resolve() for command in commands}:
        if source.suffix not in {".c", ".cc", ".cpp"} or not any(
            source.is_relative_to(ROOT / directory) for directory in ("lib", "src")
        ):
            continue
        source_text = source.read_text(encoding="utf-8")
        if any(name in source_text for name in EXECUTORS):
            candidates.add(source)
    sources = (CATALOG_SOURCE, *sorted(candidates - {CATALOG_SOURCE}))
    config = CConfig(
        clang=args.clang,
        clang_args=analysis_arguments(entry),
        executors=tuple(CExecutor(name, 1) for name in EXECUTORS),
    )
    report = check_c(sources, config)
    report.raise_for_issues()
    if report.count == 0:
        raise RuntimeError("Gotel did not discover any production SQLite call sites")
    print(f"Gotel verified {report.count} SQLite call sites")


if __name__ == "__main__":
    main()
