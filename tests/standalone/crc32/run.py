"""Test real CRC/CPU sources without Criterion or other project dependencies.

Usage: python tests/standalone/crc32/run.py [--cc clang] [--sanitize] [--bench]
Builds runtime dispatch, hardware-disabled, and simulated no-CRC CPU variants.
The benchmark compares the old bitwise/bytewise algorithms with the new paths;
it is a microbenchmark, not an end-to-end network throughput measurement.
"""
import argparse
import os
import pathlib
import platform
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[3]
HERE = pathlib.Path(__file__).resolve().parent
import sys

sys.path.insert(0, str(ROOT / "tests" / "scripts"))
from standalone_coverage import coverage_flags, standalone_output_directory
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="clang")
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--bench", action="store_true")
args = parser.parse_args()
if args.sanitize and args.bench:
    parser.error("Run benchmarks separately from sanitizer instrumentation")
arch = platform.machine().lower()
baseline = ["-march=x86-64"] if arch in ("amd64", "x86_64") else (
    ["-march=armv8-a"] if arch in ("arm64", "aarch64") else [])
with standalone_output_directory("crc32") as out:
    masked_cpu = out / "masked_cpu.c"
    masked_cpu.write_text(
        '#include <ascii-chat/platform/cpu.h>\n'
        'unsigned crc32_test_probes;\n'
        'uint32_t platform_cpu_features(void) { ++crc32_test_probes; return 0; }\n'
    )
    for name, defines, cpu in [
        ("auto", ["-DHAVE_CRC32_HW"], ROOT / "lib/platform/cpu.c"),
        ("disabled", [], ROOT / "lib/platform/cpu.c"),
        ("masked", ["-DHAVE_CRC32_HW", "-DTEST_MASKED_CPU"], masked_cpu),
    ]:
        exe = out / (name + (".exe" if os.name == "nt" else ""))
        command = [
            args.cc, "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror", *coverage_flags(),
            *baseline, *defines, "-I", str(ROOT / "include"),
            str(ROOT / "lib/network/crc32.c"), str(cpu), str(HERE / "test.c"),
            "-o", str(exe),
        ]
        if args.sanitize:
            command += ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True, cwd=out)
        subprocess.run([str(exe), *(["--bench"] if args.bench and name == "auto" else [])], check=True)
        print(f"PASS: {name}", flush=True)
