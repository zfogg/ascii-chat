#!/usr/bin/env python3
"""Plan defer batches and emit the CMake rules that connect them to targets."""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import defaultdict
from pathlib import Path


def cmake_literal(value: str) -> str:
    """Quote an arbitrary string as a CMake bracket argument."""
    separator = "="
    while f"]{separator}]" in value:
        separator += "="
    return f"[{separator}[{value}]{separator}]"


def write_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != content:
        path.write_text(content, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--records", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--stage", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    batches: dict[tuple[str, ...], list[tuple[str, str, str]]] = defaultdict(list)
    for line in args.records.read_text(encoding="utf-8").splitlines():
        target, source, output, prerequisites = line.split("\t")
        key = tuple(sorted(set(filter(None, prerequisites.split(";")))))
        batches[key].append((target, source, output))

    generated_dir = args.build_dir / "defer_transformed"
    rules: list[str] = []
    for prerequisites, entries in sorted(batches.items()):
        identity = json.dumps([args.stage, prerequisites], ensure_ascii=True)
        batch_id = hashlib.sha256(identity.encode("ascii")).hexdigest()[:24]
        manifest = generated_dir / f"batch-{batch_id}.txt"
        depfile = generated_dir / f"batch-{batch_id}.d"
        content = "".join(f"{source}\t{output}\n" for _, source, output in entries)
        write_if_changed(manifest, content)
        sources = " ".join(cmake_literal(source) for _, source, _ in entries)
        outputs = " ".join(cmake_literal(output) for _, _, output in entries)
        targets = sorted({target for target, _, _ in entries})
        name = cmake_literal(f"ascii-defer-{batch_id}")
        rules.append(f"""add_custom_command(
    OUTPUT {outputs}
    COMMAND "${{ASCIICHAT_PYTHON3_EXECUTABLE}}" "${{CMAKE_SOURCE_DIR}}/src/tooling/defer/defer.py"
        --batch {cmake_literal(manifest.as_posix())} --libclang "${{ASCIICHAT_LIBCLANG_PATH}}"
        --compilation-database "${{CMAKE_BINARY_DIR}}" --depfile {cmake_literal(depfile.as_posix())}
    DEPENDS {sources} {cmake_literal(manifest.as_posix())}
        "${{CMAKE_SOURCE_DIR}}/src/tooling/defer/defer.py"
        "${{CMAKE_SOURCE_DIR}}/src/tooling/defer/vendor/clang/cindex.py"
        "${{CMAKE_BINARY_DIR}}/compile_commands.json"
    DEPFILE {cmake_literal(depfile.as_posix())}
    COMMENT "Transforming defer source batch" VERBATIM)
add_custom_target({name} DEPENDS {outputs})
""")
        if prerequisites:
            required = " ".join(cmake_literal(item) for item in prerequisites)
            rules.append(f"add_dependencies({name} {required})\n")
        for target in targets:
            rules.append(f"add_dependencies({cmake_literal(target)} {name})\n")
    write_if_changed(args.output, "".join(rules))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
