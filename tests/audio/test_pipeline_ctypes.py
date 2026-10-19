"""Compile ABI checks for every field mirrored by the audio ctypes bindings."""

import argparse
import ctypes as c
from pathlib import Path
import subprocess
import tempfile

from pipeline_ctypes import Config, Flags
from processor_ctypes import Config as ProcessorConfig, Flags as ProcessorFlags
from test_spectral import Config as SpectralConfig


def main():
    """Compare ctypes sizes, alignment and field offsets with the native headers."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("-I", "--include", action="append", default=[], type=Path)
    args = parser.parse_args()
    layouts = {
        "spectral_config_t": SpectralConfig,
        "audio_processor_flags_t": ProcessorFlags,
        "audio_processor_config_t": ProcessorConfig,
        "client_audio_pipeline_flags_t": Flags,
        "client_audio_pipeline_config_t": Config,
    }
    checks = [
        "#define WIN32_LEAN_AND_MEAN",
        "#define NOMINMAX",
        "#include <ascii-chat/audio/client_pipeline.h>",
    ]
    for name, layout in layouts.items():
        expressions = [
            (f"sizeof({name})", c.sizeof(layout)),
            (f"_Alignof({name})", c.alignment(layout)),
        ]
        for field, field_type in layout._fields_:
            expressions += [
                (f"offsetof({name}, {field})", getattr(layout, field).offset),
                (f"sizeof((({name} *)0)->{field})", c.sizeof(field_type)),
            ]
        for expression, expected in expressions:
            checks.append(
                f'_Static_assert({expression} == {expected}, "{expression}");'
            )
    with tempfile.TemporaryDirectory(prefix="ascii-audio-abi-") as directory:
        source = Path(directory) / "layout.c"
        source.write_text("\n".join(checks) + "\n", encoding="utf-8")
        repo = Path(__file__).resolve().parents[2]
        include_flags = [
            flag
            for path in [repo / "include", repo / "deps", *args.include]
            for flag in ("-I", str(path))
        ]
        subprocess.run(
            [args.compiler, "-std=c11", "-fsyntax-only", *include_flags, str(source)],
            check=True,
        )
    print(f"PASS: {len(layouts)} audio ctypes layouts match the native ABI")


if __name__ == "__main__":
    main()
