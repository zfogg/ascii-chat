#!/usr/bin/env python3
"""Transform block-scoped defer statements into ordinary C source."""

from __future__ import annotations

import argparse
import functools
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "vendor"))
from clang import cindex

# libclang has a stable C API; LLVM 23 bindings may also describe newer symbols.
cindex.Config.set_compatibility_check(False)

from analysis import (
    Transformer,
    TransformError,
    expand_defer_wrappers,
    generated_diagnostic,
    is_cpp_source,
    is_objc_source,
    parse_wrapped,
    require_objc_exceptions,
    transform_input,
)
from manifest import Manifest, ManifestError


@functools.lru_cache(maxsize=8)
def compilation_database_files(directory: Path, mtime: int, size: int) -> dict[str, int]:
    del mtime, size
    database = cindex.CompilationDatabase.fromDirectory(str(directory))
    commands = database.getAllCompileCommands()
    counts: dict[str, int] = {}
    if commands is not None:
        for command in commands:
            key = os.path.normcase(str((Path(command.directory) / command.filename).resolve()))
            counts[key] = counts.get(key, 0) + 1
    return counts


def compilation_arguments(directory: Path, generated: Path, source: Path) -> list[str]:
    """Recover the target's compile options from CMake's compilation database."""
    directory = directory.resolve()
    database = cindex.CompilationDatabase.fromDirectory(str(directory))
    metadata = (directory / "compile_commands.json").stat()
    counts = compilation_database_files(directory, metadata.st_mtime_ns, metadata.st_size)
    generated, source = generated.resolve(), source.resolve()
    selected = generated if counts.get(os.path.normcase(str(generated)), 0) else source
    if counts.get(os.path.normcase(str(selected)), 0) != 1:
        raise TransformError(f"expected one compile command for {selected}")
    commands = database.getCompileCommands(str(selected))
    if commands is None or len(commands) != 1:
        raise TransformError(f"expected one compile command for {selected}")
    command = commands[0]
    arguments = list(command.arguments)
    if not arguments:
        raise TransformError(f"empty compile command for {generated}")
    compiler = arguments.pop(0)
    os.chdir(command.directory)
    result = ["-iquote", str(source.resolve().parent)]
    index = 0
    while index < len(arguments):
        value = arguments[index]
        if (
            value == "-Xclang"
            and arguments[index + 1 : index + 3] == ["-include-pch", "-Xclang"]
            and index + 3 < len(arguments)
        ):
            index += 4
            continue
        if value in ("-o", "-MF", "-MT", "-MQ"):
            index += 2
            continue
        if (value == "-ivfsoverlay" and index + 1 < len(arguments)
            and Path(arguments[index + 1]).resolve() == Path(str(generated.resolve().parent) + ".vfs.json").resolve()):
            index += 2
            continue
        if (
            value in ("--", "-c", "-MD", "-MMD", "-MP")
            or Path(value).resolve() == selected
        ):
            index += 1
            continue
        result.append(value)
        index += 1
    if not any(value.startswith("-resource-dir") for value in result):
        process = subprocess.run(
            [compiler, "-print-resource-dir"],
            capture_output=True,
            text=True,
            check=False,
        )
        if process.returncode:
            raise TransformError(
                f"cannot determine resource directory for {compiler}: {process.stderr}"
            )
        result.extend(["-resource-dir", process.stdout.strip()])
    return result


def paths_alias(first: Path, second: Path) -> bool:
    return first.resolve() == second.resolve() or (
        first.exists() and second.exists() and os.path.samefile(first, second)
    )


def write_bytes_if_changed(path: Path, contents: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() == contents:
        return
    temporary = path.with_name(path.name + f".{os.getpid()}.tmp")
    temporary.write_bytes(contents)
    temporary.replace(path)


def transform_with_header_overlay(
    index: cindex.Index, source: Path, output: Path, overlay: Path, arguments: list[str],
    protected_paths: list[Path], objc: bool = False,
) -> tuple[cindex.TranslationUnit, bytes]:
    contents = {source: source.read_bytes()}
    header_aliases: dict[Path, set[Path]] = {}
    header_display_paths: dict[Path, str] = {}
    root_aliases: list[tuple[Path, Path]] = []

    def add_root(path: Path) -> None:
        logical = Path(os.path.abspath(path))
        canonical = logical.resolve()
        if logical != canonical:
            root_aliases.append((logical, canonical))

    add_root(source.parent)
    working_directory = Path.cwd()
    for position, argument in enumerate(arguments[:-1]):
        if argument == "-working-directory":
            working_directory = Path(arguments[position + 1])
    position = 0
    while position < len(arguments):
        argument = arguments[position]
        include_path = ""
        if argument in ("-I", "-iquote", "-isystem", "-idirafter") and position + 1 < len(arguments):
            position += 1
            include_path = arguments[position]
        elif argument.startswith("-I") and len(argument) > 2:
            include_path = argument[2:]
        if include_path:
            path = Path(include_path)
            add_root(path if path.is_absolute() else working_directory / path)
        position += 1
    tu: cindex.TranslationUnit | None = None
    for _ in range(64):
        tu = index.parse(
            str(source), args=arguments, unsaved_files=[(str(path), text) for path, text in contents.items()],
            options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
        )
        marker_files: set[Path] = set()
        for cursor in tu.cursor.walk_preorder():
            if (cursor.kind == cindex.CursorKind.CALL_EXPR and cursor.spelling == "__ascii_defer_marker"
                and cursor.location.file is not None):
                logical = Path(os.path.abspath(cursor.location.file.name))
                canonical = logical.resolve()
                key = source if canonical == source.resolve() else canonical
                marker_files.add(key)
                aliases = header_aliases.setdefault(key, set())
                aliases.update((logical, canonical))
                header_display_paths.setdefault(key, cursor.location.file.name)
                for lexical_root, canonical_root in root_aliases:
                    for candidate, from_root, to_root in ((logical, lexical_root, canonical_root),
                                                          (logical, canonical_root, lexical_root),
                                                          (canonical, lexical_root, canonical_root),
                                                          (canonical, canonical_root, lexical_root)):
                        try:
                            aliases.add(to_root / candidate.relative_to(from_root))
                        except ValueError:
                            pass
        changed = False
        for path in marker_files:
            if path != source and path.suffix not in (".h", ".hpp", ".hh", ".hxx", ".inc", ".def"):
                raise TransformError(f"defer in included non-header files is not supported: {path}")
            if path not in contents:
                contents[path] = path.read_bytes()
                changed = True
        if changed:
            continue
        for path, text in contents.items():
            expanded = expand_defer_wrappers(tu, path, text, allow_other_files=True)
            if expanded != text:
                contents[path] = expanded
                changed = True
        if not changed:
            break
    else:
        raise TransformError("header defer wrapper expansion exceeded the recursion limit")
    assert tu is not None
    dependencies = {source}
    dependencies.update(Path(item.include.name).resolve() for item in tu.get_includes() if item.include is not None)
    if any(paths_alias(output, path) for path in dependencies):
        raise TransformError("output must not overwrite an included file")
    transformed = {
        path: Transformer(path, text, tu, allow_other_files=True,
                          display_path=str(source) if path == source else header_display_paths[path], objc=objc).transform()
        for path, text in contents.items()
    }
    validation = index.parse(str(source), args=arguments, unsaved_files=[(str(path), text) for path, text in transformed.items()])
    errors = [generated_diagnostic(item) for item in validation.diagnostics if item.severity >= cindex.Diagnostic.Error]
    if errors:
        raise TransformError(f"generated {'Objective-C' if objc else 'C'} is invalid: {errors[0]}")
    protected = [output, *dependencies, *protected_paths]
    if any(paths_alias(overlay, path) for path in protected):
        raise TransformError("header overlay must not overwrite an input or output")
    roots: list[dict[str, str]] = []
    generated: list[tuple[Path, bytes]] = []
    for path, text in transformed.items():
        if path == source:
            continue
        name = path.as_posix()
        identity = hashlib.sha256(name.encode() + b"\0" + text).hexdigest()
        target = Path(str(overlay) + ".headers") / (identity + path.suffix)
        if paths_alias(target, overlay) or any(paths_alias(target, item) for item in protected):
            raise TransformError("generated header must not overwrite an input, output, or overlay")
        if target.exists() and target.read_bytes() != text:
            raise TransformError(f"generated header path already contains unrelated data: {target}")
        for alias in sorted(header_aliases[path]):
            roots.append({"type": "file", "name": alias.as_posix(), "external-contents": target.as_posix()})
        generated.append((target, text))
    for path, text in generated:
        write_bytes_if_changed(path, text)
    mapping = {"version": 0, "use-external-names": False, "roots": roots}
    write_bytes_if_changed(overlay, json.dumps(mapping, ensure_ascii=False, indent=2).encode())
    return tu, transformed[source]


def check_libclang_version() -> None:
    version = cindex.conf.get_clang_version()
    match = re.search(r"\bclang version (\d+)\.", version)
    if not match or int(match.group(1)) < 19:
        raise TransformError(f"LLVM 19 or newer libclang required, found {version}")


def batch_main() -> int:
    """Transform a CMake batch in one interpreter and emit one combined depfile."""
    parser = argparse.ArgumentParser(
        description="Transform a batch using per-file Clang arguments or a compilation database.",
        epilog="Manifest rows are input<TAB>output with an optional <TAB>JSON array of per-file Clang arguments.",
    )
    parser.add_argument("--batch", type=Path, required=True, help="UTF-8 batch manifest")
    parser.add_argument("--libclang", type=Path)
    parser.add_argument("--compilation-database", type=Path, help="database directory or compile_commands.json")
    parser.add_argument("--depfile", type=Path)
    parser.add_argument("--dependency-stamps", action="store_true")
    parser.add_argument("--header-overlays", action="store_true")
    args = parser.parse_args()
    if args.libclang:
        cindex.Config.set_library_file(str(args.libclang.resolve()))
    manifest = args.batch.resolve()
    database = args.compilation_database.resolve() if args.compilation_database else None
    if database and database.name == "compile_commands.json":
        database = database.parent
    depfile = args.depfile.resolve() if args.depfile else None
    original_cwd = Path.cwd()
    original_argv = sys.argv
    dependencies: list[str] = []
    primary = ""
    try:
        records = Manifest.parse(manifest.read_text(encoding="utf-8"), compilation_database=database is not None).records
        sources = [record.source for record in records]
        outputs = [record.output for record in records]
        if any(paths_alias(output, source) for output in outputs for source in sources):
            raise TransformError("output must not overwrite an input source")
        stamp_paths = [Path(str(output.parent) + ".deps") for output in outputs] if args.dependency_stamps else []
        if len({stamp.resolve() for stamp in stamp_paths}) != len(stamp_paths):
            raise TransformError("dependency stamp paths must be unique")
        if depfile and any(paths_alias(stamp, depfile) for stamp in stamp_paths):
            raise TransformError("dependency stamp must not overwrite the depfile")
        part_paths = [Path(str(output.parent) + ".depfile") for output in outputs] if depfile else []
        overlay_paths = [Path(str(output.parent) + ".vfs.json") for output in outputs] if args.header_overlays else []
        if depfile and any(paths_alias(part, depfile) for part in part_paths):
            raise TransformError("batch depfile must not overwrite a per-file depfile")
        for candidate in [*([depfile] if depfile else []), *part_paths, *stamp_paths]:
            if any(paths_alias(candidate, protected) for protected in [*sources, *outputs]):
                raise TransformError("depfile must not overwrite an input source or output")
        protected = [manifest, *([depfile] if depfile else []), *part_paths, *stamp_paths, *sources, *outputs]
        for index, overlay in enumerate(overlay_paths):
            if any(paths_alias(overlay, path) for path in [*protected, *overlay_paths[:index]]):
                raise TransformError("header overlay must not overwrite a batch input or output")
        for index, record in enumerate(records):
            source, output = str(record.source), str(record.output)
            part = Path(str(Path(output).parent) + ".depfile")
            sys.argv = [
                original_argv[0],
                *(["--compilation-database", str(database)] if database else []),
                *(["--depfile", str(part)] if depfile else []),
                *(["--header-overlay", str(overlay_paths[index])] if args.header_overlays else []),
                *(["--dependency-stamp", str(Path(output).parent) + ".deps"] if args.dependency_stamps else []),
                source,
                output,
                *(["--", *record.arguments] if not database else []),
            ]
            os.chdir(original_cwd)
            current_protected = [*protected, *overlay_paths[:index], *overlay_paths[index + 1 :]] if args.header_overlays else None
            if main(current_protected) != 0:
                return 1
            if depfile:
                target, inputs = part.read_text(encoding="utf-8").split(": ", 1)
                if not primary:
                    primary = target
                dependencies.append(inputs.strip())
        if depfile:
            depfile.write_text(primary + ": " + " ".join(dependencies) + "\n", encoding="utf-8")
        return 0
    except (TransformError, ManifestError, OSError) as exc:
        print(f"defer-tool: {exc}", file=sys.stderr)
        return 1
    finally:
        sys.argv = original_argv
        os.chdir(original_cwd)


def main(protected_paths: list[Path] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--explain", metavar="SOURCE:LINE", help="explain cleanup at a source exit")
    parser.add_argument("--show-transformed", metavar="SOURCE:LINE", help="show the transformed function")
    parser.add_argument("--compilation-database", type=Path)
    parser.add_argument("--depfile", type=Path)
    parser.add_argument("--dependency-stamp", type=Path)
    parser.add_argument("--header-overlay", type=Path)
    parser.add_argument("--stdin", action="store_true", help="transform one source from stdin to stdout")
    parser.add_argument("--filename", type=Path, help="virtual input path for stdin (default: stdin.c)")
    parser.add_argument("source", type=Path, nargs="?")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument(
        "--libclang", type=Path, help="path to matching LLVM libclang shared library"
    )
    parser.add_argument(
        "clang_args", nargs=argparse.REMAINDER, help="Clang arguments after --"
    )
    explain_args: list[str] = []
    stream_args: list[str] = []
    argv = sys.argv[1:]
    if any(arg.split("=", 1)[0] in ("--explain", "--show-transformed") for arg in argv) and "--" in argv:
        separator = argv.index("--")
        explain_args = argv[separator + 1 :]
        argv = argv[:separator]
    elif "--stdin" in argv and "--" in argv:
        separator = argv.index("--")
        stream_args = argv[separator + 1 :]
        argv = argv[:separator]
    args = parser.parse_args(argv)
    if args.libclang:
        cindex.Config.set_library_file(str(args.libclang.resolve()))
    clang_args = explain_args if args.explain or args.show_transformed else stream_args if args.stdin else args.clang_args
    if clang_args[:1] == ["--"]:
        clang_args = clang_args[1:]
    try:
        dash_stream = args.source == Path("-") and args.output == Path("-")
        stream = args.stdin or dash_stream
        if args.stdin and (args.source or args.output):
            raise TransformError("--stdin does not accept source or output paths")
        if args.filename and not stream:
            raise TransformError("--filename requires stdin mode")
        if stream and (args.explain or args.show_transformed or args.depfile or args.dependency_stamp or args.header_overlay):
            raise TransformError("stdin mode cannot be combined with batch, scan, planning, inspection, or output files")
        if stream:
            path = (args.filename or Path("stdin.c")).resolve()
            if args.compilation_database:
                if clang_args:
                    raise TransformError("explicit arguments cannot be combined with --compilation-database")
                database = args.compilation_database
                if database.name == "compile_commands.json":
                    database = database.parent
                clang_args = compilation_arguments(database, path, path)
            objc = is_objc_source(path, clang_args)
            if objc:
                require_objc_exceptions(clang_args)
            check_libclang_version()
            source = sys.stdin.buffer.read()
            _, output = transform_input(cindex.Index.create(), path, source,
                                        [*clang_args, "-DASCIICHAT_DEFER_PARSING=1"], objc)
            sys.stdout.buffer.write(output)
            sys.stdout.buffer.flush()
            return 0
        if args.explain or args.show_transformed:
            if args.explain and args.show_transformed:
                raise TransformError("--explain and --show-transformed cannot be combined")
            path, separator, line = (args.explain or args.show_transformed).rpartition(":")
            if not separator or not path or not line.isascii() or not line.isdigit() or int(line) == 0:
                raise TransformError("--explain expects source:line with a positive line number")
            if args.source or args.output or args.depfile or args.compilation_database or args.header_overlay:
                raise TransformError("inspection accepts only explicit Clang arguments")
            source_path = Path(path).resolve()
            objc = is_objc_source(source_path, clang_args)
            if objc:
                require_objc_exceptions(clang_args)
            if is_cpp_source(source_path, clang_args):
                raise TransformError("C++ defer inspection is not supported; C++ uses native scope guards")
            source = source_path.read_bytes()
            clang_args.append("-DASCIICHAT_DEFER_PARSING=1")
            index = cindex.Index.create()
            tu, source = parse_wrapped(index, source_path, clang_args, source)
            transformer = Transformer(source_path, source, tu, objc=objc)
            inspection = transformer.show_transformed(int(line)) if args.show_transformed else transformer.explain(int(line))
            transformed = Transformer(source_path, source, tu, objc=objc).transform()
            validation = index.parse(str(source_path), args=clang_args, unsaved_files=[(str(source_path), transformed)],
                                     options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
            errors = [generated_diagnostic(item) for item in validation.diagnostics if item.severity >= cindex.Diagnostic.Error]
            if errors:
                raise TransformError(f"generated {'Objective-C' if objc else 'C'} is invalid: {errors[0]}")
            if isinstance(inspection, bytes):
                sys.stdout.buffer.write(inspection)
            else:
                print(inspection, end="")
            return 0
        if args.source is None or args.output is None:
            raise TransformError("expected source and output paths")
        args.source = Path(os.path.abspath(args.source)) if args.header_overlay else args.source.resolve()
        args.output = args.output.resolve()
        if args.depfile:
            args.depfile = args.depfile.resolve()
        if args.dependency_stamp:
            args.dependency_stamp = args.dependency_stamp.resolve()
        if args.header_overlay:
            args.header_overlay = args.header_overlay.resolve()
            if protected_paths is None and (args.depfile or args.dependency_stamp):
                raise TransformError("--header-overlay cannot yet be combined with dependency files")
        if paths_alias(args.output, args.source):
            raise TransformError("output must not overwrite the input source")
        if args.depfile and any(paths_alias(args.depfile, p) for p in (args.source, args.output)):
            raise TransformError("depfile must not overwrite an input source or output")
        if args.dependency_stamp and any(paths_alias(args.dependency_stamp, p) for p in (args.source, args.output)):
            raise TransformError("dependency stamp must not overwrite an input source or output")
        if args.dependency_stamp and args.depfile and paths_alias(args.dependency_stamp, args.depfile):
            raise TransformError("dependency stamp must not overwrite the depfile")
        if args.compilation_database:
            if clang_args:
                raise TransformError("explicit arguments cannot be combined with --compilation-database")
            if args.compilation_database.name == "compile_commands.json":
                args.compilation_database = args.compilation_database.parent
            clang_args = compilation_arguments(
                args.compilation_database, args.output, args.source
            )
        objc = is_objc_source(args.source, clang_args)
        if objc:
            require_objc_exceptions(clang_args)
        source = args.source.read_bytes()
        clang_args.append("-DASCIICHAT_DEFER_PARSING=1")
        check_libclang_version()
        index = cindex.Index.create()
        if args.header_overlay:
            if is_cpp_source(args.source, clang_args):
                raise TransformError("C++ uses native scope guards and does not require a header overlay")
            tu, output = transform_with_header_overlay(index, args.source, args.output, args.header_overlay,
                                                       clang_args, protected_paths or [], objc)
        else:
            tu, output = transform_input(index, args.source, source, clang_args, objc)
        includes = {str(args.source)}
        includes.update(str(inc.include) for inc in tu.get_includes() if inc.include is not None)
        if any(paths_alias(args.output, Path(path)) for path in includes):
            raise TransformError("output must not overwrite an included file")
        write_bytes_if_changed(args.output, output)
        if args.depfile or args.dependency_stamp:

            def escape(path: str) -> str:
                return (
                    path.replace("\\", "/")
                    .replace("$", "$$")
                    .replace("#", "\\#")
                    .replace(" ", "\\ ")
                    .replace(":", "\\:")
                )

            if args.depfile:
                depfile = (
                    escape(str(args.output))
                    + ": "
                    + " ".join(escape(path) for path in sorted(includes))
                    + "\n"
                )
                args.depfile.write_text(depfile, encoding="utf-8")
            if args.dependency_stamp:
                if any(paths_alias(args.dependency_stamp, Path(path)) for path in includes):
                    raise TransformError("dependency stamp must not overwrite an included file")
                stamp = "".join(
                    f"{path}\t{Path(path).stat().st_mtime_ns}\t{Path(path).stat().st_size}\t{Path(path).resolve()}\n"
                    for path in sorted(includes)
                )
                if not args.dependency_stamp.exists() or args.dependency_stamp.read_text(encoding="utf-8") != stamp:
                    args.dependency_stamp.write_text(stamp, encoding="utf-8")
    except (
        TransformError,
        cindex.LibclangError,
        cindex.TranslationUnitLoadError,
        cindex.CompilationDatabaseError,
        OSError,
    ) as exc:
        print(f"defer-tool: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(batch_main() if any(arg == "--batch" or arg.startswith("--batch=") for arg in sys.argv[1:]) else main())
