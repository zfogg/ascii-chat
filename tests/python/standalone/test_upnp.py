"""Compile UPnP against deterministic gateway API variants."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest
from ascii_chat_tests.standalone import coverage_flags, standalone_output_directory

ROOT = Path(__file__).resolve().parents[3]
HERE = ROOT / "tests/standalone/upnp"
pytestmark = pytest.mark.unit


@pytest.mark.parametrize("variant,defines", [
    ("upnp-5arg", ["HAVE_MINIUPNPC"]),
    ("upnp-7arg", ["HAVE_MINIUPNPC", "MINIUPNPC_GETVALIDIGD_7ARG"]),
    ("natpmp", ["HAVE_MINIUPNPC", "MINIUPNPC_GETVALIDIGD_7ARG", "__APPLE__"]),
    ("unavailable", []),
])
def test_upnp_gateway_variants(c_compiler, variant, defines):
    with standalone_output_directory("upnp-" + variant) as out:
        headers = {"ascii-chat/common.h": "common.h", "ascii-chat/log/log.h": "common.h",
                   "ascii-chat/util/time.h": "common.h", "ascii-chat/otel.h": "otel.h",
                   "miniupnpc/miniupnpc.h": "gateway.h", "miniupnpc.h": "gateway.h",
                   "upnpcommands.h": "gateway.h", "upnperrors.h": "gateway.h",
                   "miniupnpc/upnpcommands.h": "gateway.h", "miniupnpc/upnperrors.h": "gateway.h", "natpmp.h": "gateway.h"}
        for name, source in headers.items():
            target = out / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(HERE / source, target)
        for name in ("ascii-chat/network/nat/upnp.h", "ascii-chat/common/limits.h"):
            target = out / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / "include" / name, target)
        exe = out / (variant + (".exe" if os.name == "nt" else ""))
        command = [c_compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *coverage_flags(), "-I", str(out),
                   *["-D" + flag for flag in defines], str(ROOT / "lib/network/nat/upnp.c"), str(HERE / "test.c"), "-o", str(exe)]
        subprocess.run(command, check=True, cwd=out, timeout=60)
        subprocess.run([str(exe)], check=True, timeout=30)
