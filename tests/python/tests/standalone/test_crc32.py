"""CRC32 dispatch and hardware-disabled dependency-isolated builds."""
import os
from pathlib import Path
import platform
import subprocess

import pytest
from ascii_chat_tests.standalone import coverage_flags, standalone_output_directory

ROOT = Path(__file__).resolve().parents[3]
HERE = ROOT / "tests/standalone/crc32"
pytestmark = pytest.mark.unit


@pytest.mark.parametrize("name,defines", [
    ("auto", ["-DHAVE_CRC32_HW"]), ("disabled", []),
    ("masked", ["-DHAVE_CRC32_HW", "-DTEST_MASKED_CPU"]),
])
def test_crc32_dispatch(c_compiler, name, defines):
    arch = platform.machine().lower()
    baseline = ["-march=x86-64"] if arch in ("amd64", "x86_64") else (["-march=armv8-a"] if arch in ("arm64", "aarch64") else [])
    with standalone_output_directory("crc32-" + name) as out:
        masked = out / "masked_cpu.c"
        masked.write_text('#include <ascii-chat/platform/cpu.h>\nunsigned crc32_test_probes;\n'
                          'uint32_t platform_cpu_features(void) { ++crc32_test_probes; return 0; }\n')
        cpu = masked if name == "masked" else ROOT / "lib/platform/cpu.c"
        exe = out / (name + (".exe" if os.name == "nt" else ""))
        command = [c_compiler, "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror", *coverage_flags(),
                   *baseline, *defines, "-I", str(ROOT / "include"), str(ROOT / "lib/network/crc32.c"),
                   str(cpu), str(HERE / "test.c"), "-o", str(exe)]
        subprocess.run(command, check=True, cwd=out, timeout=60)
        subprocess.run([str(exe)], check=True, timeout=30)
