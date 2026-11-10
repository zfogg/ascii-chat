"""Parse defer batch manifests without reading or modifying source files."""

import json
from dataclasses import dataclass
from pathlib import Path


class ManifestError(ValueError):
    """A batch manifest is malformed or incompatible with its options."""


@dataclass(frozen=True)
class ManifestRecord:
    source: Path
    output: Path
    arguments: list[str]


@dataclass(frozen=True)
class Manifest:
    records: list[ManifestRecord]

    @staticmethod
    def parse(text: str, *, compilation_database: bool = False) -> "Manifest":
        records = []
        lines = text.split("\n")
        if lines[-1] == "":
            lines.pop()
        for line in lines:
            line = line.removesuffix("\r")
            fields = line.split("\t")
            if len(fields) not in (2, 3) or not fields[0] or not fields[1]:
                raise ManifestError("invalid source/output batch record")
            arguments = []
            if len(fields) == 3:
                if compilation_database:
                    raise ManifestError("per-file arguments cannot be combined with --compilation-database")
                try:
                    arguments = json.loads(fields[2])
                except json.JSONDecodeError as exc:
                    raise ManifestError("batch arguments must be a JSON array of strings") from exc
                if not isinstance(arguments, list) or any(not isinstance(arg, str) or "\0" in arg for arg in arguments):
                    raise ManifestError("batch arguments must be a JSON array of strings without NUL bytes")
            records.append(ManifestRecord(Path(fields[0]), Path(fields[1]), arguments))
        return Manifest(records)
