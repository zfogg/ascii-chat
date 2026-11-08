#!/usr/bin/env python3
"""Transform block-scoped defer statements into ordinary C source."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "vendor"))
from clang import cindex

# libclang has a stable C API; LLVM 23 bindings may also describe newer symbols.
cindex.Config.set_compatibility_check(False)


class TransformError(Exception):
    """The source cannot be transformed without changing its behavior."""


@dataclass
class DeferredAction:
    start: int
    action_start: int
    flag: bytes
    action: bytes
    identifiers: set[str]


@dataclass
class Scope:
    start: int
    defers: list[DeferredAction] = field(default_factory=list)


class Transformer:
    def __init__(self, path: Path, source: bytes, tu: cindex.TranslationUnit) -> None:
        self.path = path.resolve()
        self.source = source
        self.tu = tu
        self.edits: list[tuple[int, int, bytes]] = []
        self.macros: dict[int, cindex.Cursor] = {}
        self.macro_definitions: dict[str, list[cindex.Cursor]] = {}
        self.seen: set[int] = set()
        self.scopes: list[Scope] = []
        self.controls: list[tuple[str, int]] = []
        self.function_void = False
        self.function_prelude = b""
        self.return_capture_type = b""
        self.labels: dict[str, tuple[int, tuple[int, ...]]] = {}
        self.defer_scopes: dict[int, int] = {}
        self.declarations: list[tuple[str, int, tuple[int, ...], int]] = []
        directive = rb"(?m)^[ \t]*#[ \t]*(define|undef|include|include_next|import|pragma)\b[ \t]*([A-Za-z_][A-Za-z_0-9]*)?"
        self.directives = [
            (match.start(), match.group(1), match.group(2))
            for match in re.finditer(directive, source)
        ]
        if b"__ascii_defer_" in source:
            raise TransformError(
                "reserved identifier prefix __ascii_defer_ occurs in input"
            )
        root = tu.cursor
        if root is None:
            raise TransformError("Clang returned no translation unit cursor")
        for cursor in root.get_children():
            if cursor.kind == cindex.CursorKind.MACRO_DEFINITION:
                self.macro_definitions.setdefault(cursor.spelling, []).append(cursor)
            if (
                cursor.kind == cindex.CursorKind.MACRO_INSTANTIATION
                and cursor.spelling == "defer"
                and self.in_file(cursor)
            ):
                self.macros[cursor.extent.start.offset] = cursor
        if self.macros and re.search(rb"(?m)^[ \t]*#[ \t]*(?:line\b|[0-9])", source):
            raise TransformError(
                "source-level line directives cannot be preserved safely"
            )

    def in_file(self, cursor: cindex.Cursor) -> bool:
        file = cursor.location.file
        return bool(file and Path(file.name).resolve() == self.path)

    def after_semicolon(self, end: int) -> int:
        pos = end
        while pos < len(self.source):
            if chr(self.source[pos]).isspace():
                pos += 1
            elif self.source[pos : pos + 2] == b"/*":
                end_comment = self.source.find(b"*/", pos + 2)
                if end_comment < 0:
                    raise TransformError(f"unterminated comment at byte {pos}")
                pos = end_comment + 2
            elif self.source[pos : pos + 2] == b"//":
                newline = self.source.find(b"\n", pos + 2)
                pos = len(self.source) if newline < 0 else newline + 1
            else:
                break
        if self.source[pos : pos + 1] != b";":
            raise TransformError(f"expected semicolon at byte {end}")
        return pos + 1

    def expanded_identifiers(self, tokens: list[cindex.Token]) -> set[str]:
        """Include identifiers hidden inside macros used by a deferred action."""
        found = {
            token.spelling
            for token in tokens
            if token.kind == cindex.TokenKind.IDENTIFIER
        }
        pending = list(found)
        visited: set[str] = set()
        forbidden = {"return", "goto", "break", "continue", "defer"}
        while pending:
            name = pending.pop()
            if name == "__COUNTER__":
                raise TransformError(
                    "__COUNTER__ in deferred cleanup cannot be duplicated safely"
                )
            if name in visited:
                continue
            visited.add(name)
            for definition in self.macro_definitions.get(name, []):
                body = list(definition.get_tokens())
                if not body:
                    continue
                start = 1
                parameters: set[str] = set()
                if (
                    len(body) > 1
                    and body[1].spelling == "("
                    and body[0].extent.end.offset == body[1].extent.start.offset
                ):
                    start = 2
                    while start < len(body) and body[start].spelling != ")":
                        if body[start].kind == cindex.TokenKind.IDENTIFIER:
                            parameters.add(body[start].spelling)
                        start += 1
                    start += 1
                for token in body[start:]:
                    if token.spelling == "##":
                        raise TransformError(
                            f"cleanup macro {name!r} uses token pasting"
                        )
                    if token.spelling in forbidden:
                        raise TransformError(
                            f"cleanup macro {name!r} contains control transfer"
                        )
                    if (
                        token.kind == cindex.TokenKind.IDENTIFIER
                        and token.spelling not in parameters
                        and token.spelling not in found
                    ):
                        found.add(token.spelling)
                        pending.append(token.spelling)
        return found

    def line_directive(self, offset: int) -> bytes:
        line = self.source.count(b"\n", 0, offset) + 1
        filename = self.path.as_posix().replace("\\", "\\\\").replace('"', '\\"')
        return f'\n#line {line} "{filename}"\n'.encode()

    def cleanup(self, scopes: list[Scope], offset: int) -> bytes:
        result = []
        current = tuple(scope.start for scope in self.scopes)
        for scope in reversed(scopes):
            owner = current[: current.index(scope.start) + 1]
            for item in reversed(scope.defers):
                if item.start < offset:
                    for directive_offset, kind, name in self.directives:
                        if item.start < directive_offset < offset and (
                            kind not in (b"define", b"undef")
                            or (name and name.decode() in item.identifiers)
                        ):
                            raise TransformError(
                                f"preprocessor state affecting cleanup may change at byte {directive_offset}"
                            )
                    for name, declaration, path, limit in self.declarations:
                        if (
                            name in item.identifiers
                            and item.start < declaration < offset < limit
                            and path[: len(owner)] == owner
                            and current[: len(path)] == path
                        ):
                            raise TransformError(
                                f"cleanup identifier {name!r} may be shadowed at exit byte {offset}"
                            )
                    result.append(
                        b"if ("
                        + item.flag
                        + b") { "
                        + self.line_directive(item.action_start)
                        + item.action
                        + b"\n; } "
                        + self.line_directive(offset)
                    )
        return b"".join(result)

    def index_control_flow(
        self,
        cursor: cindex.Cursor,
        scopes: tuple[int, ...] = (),
        limit: int = sys.maxsize,
    ) -> None:
        kind = cursor.kind
        start = cursor.extent.start.offset
        if kind == cindex.CursorKind.DO_STMT and start in self.macros:
            if scopes:
                self.defer_scopes[start] = scopes[-1]
            return
        if kind == cindex.CursorKind.COMPOUND_STMT:
            scopes = (*scopes, start)
        if kind == cindex.CursorKind.FOR_STMT:
            limit = min(limit, cursor.extent.end.offset)
        if kind == cindex.CursorKind.LABEL_STMT:
            self.labels[cursor.spelling] = (start, scopes)
        if (
            kind
            in (
                cindex.CursorKind.VAR_DECL,
                cindex.CursorKind.TYPEDEF_DECL,
                cindex.CursorKind.ENUM_CONSTANT_DECL,
                cindex.CursorKind.STRUCT_DECL,
                cindex.CursorKind.UNION_DECL,
                cindex.CursorKind.ENUM_DECL,
                cindex.CursorKind.FUNCTION_DECL,
            )
            and scopes
        ):
            self.declarations.append((cursor.spelling, start, scopes, limit))
        for child in cursor.get_children():
            self.index_control_flow(child, scopes, limit)

    def require_keyword(self, offset: int, keyword: bytes) -> None:
        """Reject macro expansions whose spelling is not a standalone keyword."""
        suffix = self.source[offset + len(keyword) : offset + len(keyword) + 1]
        if self.source[offset : offset + len(keyword)] != keyword or (
            suffix and (suffix.isalnum() or suffix == b"_" or suffix[0] >= 128)
        ):
            raise TransformError(
                f"control transfer from a macro cannot be transformed (byte {offset})"
            )

    def visit(
        self, cursor: cindex.Cursor, parent_kind: cindex.CursorKind | None = None
    ) -> None:
        kind = cursor.kind
        start = cursor.extent.start.offset
        end = cursor.extent.end.offset
        if (
            kind == cindex.CursorKind.FUNCTION_DECL
            and cursor.is_definition()
            and self.in_file(cursor)
        ):
            self.function_void = cursor.result_type.kind == cindex.TypeKind.VOID
            self.function_prelude = b""
            self.return_capture_type = b""
            if not self.function_void:
                fn_type = f"__ascii_defer_function_{start}"
                declarations = [f"typedef __typeof__(&{cursor.spelling}) {fn_type};"]
                call_arguments = []
                for index, parameter in enumerate(cursor.get_arguments()):
                    if not parameter.spelling:
                        raise TransformError(
                            f"unnamed parameter in defer function at byte {start}"
                        )
                    parameter_type = parameter.type.get_canonical()
                    while parameter_type.kind in (
                        cindex.TypeKind.POINTER,
                        cindex.TypeKind.CONSTANTARRAY,
                        cindex.TypeKind.INCOMPLETEARRAY,
                    ):
                        if parameter_type.kind == cindex.TypeKind.POINTER:
                            parameter_type = (
                                parameter_type.get_pointee().get_canonical()
                            )
                        else:
                            parameter_type = (
                                parameter_type.get_array_element_type().get_canonical()
                            )
                    if parameter_type.kind in (
                        cindex.TypeKind.VARIABLEARRAY,
                        cindex.TypeKind.DEPENDENTSIZEDARRAY,
                    ):
                        raise TransformError(
                            f"variably modified parameter in defer function at byte {start}"
                        )
                    name = f"__ascii_defer_argument_{start}_{index}"
                    declarations.append(
                        f"typedef __typeof__({parameter.spelling}) {name};"
                    )
                    call_arguments.append(f"({name}){{0}}")
                self.function_prelude = " ".join(declarations).encode()
                arguments = ", ".join(call_arguments)
                self.return_capture_type = (
                    f"__typeof__((({fn_type})0)({arguments}))".encode()
                )
            for child in cursor.get_children():
                self.visit(child, kind)
            return
        if not self.scopes and kind != cindex.CursorKind.COMPOUND_STMT:
            return
        if start in self.macros and kind == cindex.CursorKind.DO_STMT:
            if parent_kind != cindex.CursorKind.COMPOUND_STMT:
                raise TransformError(
                    f"defer at byte {start} must be a direct statement in a block"
                )
            macro = self.macros[start]
            macro_end = macro.extent.end.offset
            text = self.source[start:macro_end]
            tokens = list(self.tu.get_tokens(extent=macro.extent))
            opening = next(
                (token for token in tokens[1:] if token.spelling == "("), None
            )
            open_paren = opening.extent.start.offset - start if opening else -1
            if open_paren < 0 or not text.endswith(b")"):
                raise TransformError(f"cannot parse defer at byte {start}")
            raw_action = text[open_paren + 1 : -1]
            action_start = (
                start + open_paren + 1 + len(raw_action) - len(raw_action.lstrip())
            )
            action = raw_action.strip()
            if not action:
                raise TransformError(f"empty defer at byte {start}")
            forbidden = {"return", "goto", "break", "continue", "defer"}
            for token in tokens[1:]:
                if token.spelling in forbidden:
                    raise TransformError(
                        f"control transfer or nested defer inside cleanup at byte {start}"
                    )
            identifiers = self.expanded_identifiers(tokens[1:])
            flag = f"__ascii_defer_active_{start}".encode()
            self.scopes[-1].defers.append(
                DeferredAction(start, action_start, flag, action, identifiers)
            )
            self.edits.append((start, self.after_semicolon(macro_end), flag + b" = 1;"))
            self.seen.add(start)
            return
        if kind == cindex.CursorKind.COMPOUND_STMT:
            if parent_kind == cindex.CursorKind.FUNCTION_DECL and self.function_prelude:
                self.edits.append((start + 1, start + 1, self.function_prelude + b" "))
            scope = Scope(start)
            self.scopes.append(scope)
            for child in cursor.get_children():
                self.visit(child, kind)
            actions = scope.defers
            if actions and parent_kind == cindex.CursorKind.SWITCH_STMT:
                raise TransformError(
                    f"defer directly in switch body is unsupported (byte {start}); use a case block"
                )
            if actions:
                declarations = b" ".join(
                    b"int " + item.flag + b" = 0;" for item in actions
                )
                self.edits.append((start + 1, start + 1, declarations + b" "))
                self.edits.append((end - 1, end - 1, self.cleanup([scope], end)))
            self.scopes.pop()
            return
        if kind == cindex.CursorKind.INDIRECT_GOTO_STMT:
            raise TransformError(f"computed goto cannot be transformed (byte {start})")
        if kind == cindex.CursorKind.GOTO_STMT:
            refs = [
                child.spelling
                for child in cursor.get_children()
                if child.kind == cindex.CursorKind.LABEL_REF
            ]
            if len(refs) != 1 or refs[0] not in self.labels:
                raise TransformError(f"cannot determine goto target at byte {start}")
            destination, target_scopes = self.labels[refs[0]]
            current_scopes = tuple(scope.start for scope in self.scopes)
            if current_scopes[: len(target_scopes)] != target_scopes:
                raise TransformError(
                    f"goto enters a block with defer analysis (byte {start})"
                )
            if destination < start and any(
                destination <= offset < start and owner in target_scopes
                for offset, owner in self.defer_scopes.items()
            ):
                raise TransformError(
                    f"backward goto can register defer repeatedly (byte {start})"
                )
            cleanup = self.cleanup(self.scopes[len(target_scopes) :], start)
            if cleanup:
                self.require_keyword(start, b"goto")
                stop = self.after_semicolon(end)
                self.edits.append(
                    (start, stop, b"{ " + cleanup + self.source[start:stop] + b" }")
                )
            return
        if kind == cindex.CursorKind.RETURN_STMT:
            cleanup = self.cleanup(self.scopes, start)
            if cleanup:
                self.require_keyword(start, b"return")
                stop = self.after_semicolon(end)
                raw_expr = self.source[start + 6 : end]
                expr_start = start + 6 + len(raw_expr) - len(raw_expr.lstrip())
                expr = raw_expr.strip()
                if expr:
                    expr = self.line_directive(expr_start) + expr
                if expr and self.function_void:
                    replacement = b"{ " + expr + b"; " + cleanup + b"return; }"
                elif expr:
                    temp = f"__ascii_defer_return_{start}".encode()
                    replacement = (
                        b"{ "
                        + self.return_capture_type
                        + b" "
                        + temp
                        + b" = ("
                        + expr
                        + b"); "
                        + cleanup
                        + b"return "
                        + temp
                        + b"; }"
                    )
                else:
                    replacement = b"{ " + cleanup + b"return; }"
                self.edits.append((start, stop, replacement))
            return
        if kind in (cindex.CursorKind.BREAK_STMT, cindex.CursorKind.CONTINUE_STMT):
            targets = (
                self.controls
                if kind == cindex.CursorKind.BREAK_STMT
                else [x for x in self.controls if x[0] == "loop"]
            )
            if not targets:
                raise TransformError(
                    f"cannot determine control-flow target at byte {start}"
                )
            cleanup = self.cleanup(self.scopes[targets[-1][1] :], start)
            if cleanup:
                keyword = (
                    b"break" if kind == cindex.CursorKind.BREAK_STMT else b"continue"
                )
                self.require_keyword(start, keyword)
                stop = self.after_semicolon(end)
                self.edits.append(
                    (start, stop, b"{ " + cleanup + self.source[start:stop] + b" }")
                )
            return
        control_kind = None
        if kind in (
            cindex.CursorKind.FOR_STMT,
            cindex.CursorKind.WHILE_STMT,
            cindex.CursorKind.DO_STMT,
        ):
            control_kind = "loop"
        elif kind == cindex.CursorKind.SWITCH_STMT:
            control_kind = "switch"
        if control_kind:
            self.controls.append((control_kind, len(self.scopes)))
        for child in cursor.get_children():
            self.visit(child, kind)
        if control_kind:
            self.controls.pop()

    def transform(self) -> bytes:
        errors = [
            str(d) for d in self.tu.diagnostics if d.severity >= cindex.Diagnostic.Error
        ]
        if errors:
            raise TransformError("Clang could not parse source:\n" + "\n".join(errors))
        # Only visit functions with a source-level defer, so unrelated gotos remain valid.
        root = self.tu.cursor
        if root is None:
            raise TransformError("Clang returned no translation unit cursor")
        for cursor in root.get_children():
            if (
                cursor.kind == cindex.CursorKind.FUNCTION_DECL
                and cursor.is_definition()
                and self.in_file(cursor)
            ):
                start, end = cursor.extent.start.offset, cursor.extent.end.offset
                if any(start <= offset < end for offset in self.macros):
                    self.labels.clear()
                    self.defer_scopes.clear()
                    self.declarations.clear()
                    self.index_control_flow(cursor)
                    self.visit(cursor)
        missed = set(self.macros) - self.seen
        if missed:
            raise TransformError(
                f"unhandled defer macro expansion(s) at byte offsets {sorted(missed)}"
            )
        output = self.source
        for start, end, replacement in sorted(
            self.edits, key=lambda edit: (edit[0], edit[1]), reverse=True
        ):
            output = (
                output[:start] + replacement + self.line_directive(end) + output[end:]
            )
        if output.startswith(b"\xef\xbb\xbf"):
            return output[:3] + self.line_directive(0) + output[3:]
        return self.line_directive(0) + output


def compilation_arguments(directory: Path, generated: Path, source: Path) -> list[str]:
    """Recover the target's compile options from CMake's compilation database."""
    database = cindex.CompilationDatabase.fromDirectory(str(directory.resolve()))
    commands = database.getCompileCommands(str(generated.resolve()))
    if commands is None or len(commands) != 1:
        raise TransformError(f"expected one compile command for {generated}")
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
        if (
            value in ("--", "-c", "-MD", "-MMD", "-MP")
            or Path(value).resolve() == generated.resolve()
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


def batch_main() -> int:
    """Transform a CMake batch in one interpreter and emit one combined depfile."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--batch", type=Path, required=True)
    parser.add_argument("--libclang", type=Path, required=True)
    parser.add_argument("--compilation-database", type=Path, required=True)
    parser.add_argument("--depfile", type=Path, required=True)
    args = parser.parse_args()
    cindex.Config.set_library_file(str(args.libclang.resolve()))
    manifest = args.batch.resolve()
    database = args.compilation_database.resolve()
    depfile = args.depfile.resolve()
    original_cwd = Path.cwd()
    original_argv = sys.argv
    dependencies: list[str] = []
    primary = ""
    try:
        for line in manifest.read_text(encoding="utf-8").splitlines():
            source, output = line.split("\t")
            part = Path(output + ".d")
            sys.argv = [
                original_argv[0],
                "--compilation-database",
                str(database),
                "--depfile",
                str(part),
                source,
                output,
            ]
            os.chdir(original_cwd)
            if main() != 0:
                return 1
            target, inputs = part.read_text(encoding="utf-8").split(": ", 1)
            if not primary:
                primary = target
            dependencies.append(inputs.strip())
        depfile.write_text(
            primary + ": " + " ".join(dependencies) + "\n", encoding="utf-8"
        )
        return 0
    finally:
        sys.argv = original_argv
        os.chdir(original_cwd)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compilation-database", type=Path)
    parser.add_argument("--depfile", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--libclang", type=Path, help="path to matching LLVM libclang shared library"
    )
    parser.add_argument(
        "clang_args", nargs=argparse.REMAINDER, help="Clang arguments after --"
    )
    args = parser.parse_args()
    if args.libclang:
        cindex.Config.set_library_file(str(args.libclang.resolve()))
    clang_args = args.clang_args
    if clang_args[:1] == ["--"]:
        clang_args = clang_args[1:]
    try:
        args.source = args.source.resolve()
        args.output = args.output.resolve()
        if args.depfile:
            args.depfile = args.depfile.resolve()
        if args.output == args.source:
            raise TransformError("output must not overwrite the input source")
        if args.compilation_database:
            clang_args = compilation_arguments(
                args.compilation_database, args.output, args.source
            )
        source = args.source.read_bytes()
        clang_args.append("-DASCIICHAT_DEFER_PARSING=1")
        version = cindex.conf.get_clang_version()
        match = re.search(r"\bclang version (\d+)\.", version)
        if not match or int(match.group(1)) < 19:
            raise TransformError(f"LLVM 19 or newer libclang required, found {version}")
        tu = cindex.Index.create().parse(
            str(args.source.resolve()),
            args=clang_args,
            options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
        )
        output = Transformer(args.source, source, tu).transform()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        if not args.output.exists() or args.output.read_bytes() != output:
            temporary = args.output.with_name(args.output.name + f".{os.getpid()}.tmp")
            temporary.write_bytes(output)
            temporary.replace(args.output)
        if args.depfile:

            def escape(path: str) -> str:
                return (
                    path.replace("\\", "/")
                    .replace("$", "$$")
                    .replace("#", "\\#")
                    .replace(" ", "\\ ")
                    .replace(":", "\\:")
                )

            includes = {str(args.source)}
            includes.update(
                str(inc.include) for inc in tu.get_includes() if inc.include is not None
            )
            depfile = (
                escape(str(args.output))
                + ": "
                + " ".join(escape(path) for path in sorted(includes))
                + "\n"
            )
            args.depfile.write_text(depfile, encoding="utf-8")
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
    raise SystemExit(batch_main() if "--batch" in sys.argv[1:] else main())
