import pytest
from types import SimpleNamespace
"""Compile ABI checks for every field mirrored by the audio ctypes bindings."""

import argparse
import ctypes as c
from pathlib import Path
import subprocess
import tempfile

from ascii_chat_tests.pipeline_ctypes import Config, Flags
from ascii_chat_tests.processor_ctypes import Config as ProcessorConfig, Flags as ProcessorFlags
from ascii_chat_tests.audio_spectral import Config as SpectralConfig


pytestmark = pytest.mark.unit


def test_audio_ctypes_layouts(c_compiler, request):
    args = SimpleNamespace(compiler=c_compiler, include=[Path(value) for value in request.config.getoption("--native-include")])
    layouts = {
        "spectral_config_t": SpectralConfig,
        "audio_processor_flags_t": ProcessorFlags,
        "audio_processor_config_t": ProcessorConfig,
        "client_audio_pipeline_flags_t": Flags,
        "client_audio_pipeline_config_t": Config,
    }
    checks = [
        "#define _GNU_SOURCE",
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
        repo = Path(__file__).resolve().parents[3]
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
