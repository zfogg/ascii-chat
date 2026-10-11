#!/usr/bin/env python3
"""Run the production transport benchmark and save timings plus process CPU usage."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import subprocess
import time


def cpu_seconds(process, before):
    if os.name != "nt":
        import resource
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        return usage.ru_utime + usage.ru_stime - before
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    function = kernel32.GetProcessTimes
    function.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    function.restype = wintypes.BOOL
    values = [wintypes.FILETIME() for _ in range(4)]
    if not function(wintypes.HANDLE(int(process._handle)), *(ctypes.byref(v) for v in values)):
        raise ctypes.WinError(ctypes.get_last_error())
    return sum((v.dwHighDateTime << 32) | v.dwLowDateTime for v in values[2:]) / 10_000_000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    records = []
    for run in range(args.runs * 2):
        mode = "assembled" if run % 2 == 0 else "vectored"
        before_cpu = 0
        if os.name != "nt":
            import resource
            usage = resource.getrusage(resource.RUSAGE_CHILDREN)
            before_cpu = usage.ru_utime + usage.ru_stime
        start = time.monotonic()
        process = subprocess.Popen([str(args.binary.resolve()), "--benchmark", mode],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            output, _ = process.communicate(timeout=120)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate()
            args.output.with_suffix(f".run{run + 1}.log").write_bytes(output)
            raise
        wall = time.monotonic() - start
        cpu = cpu_seconds(process, before_cpu)
        log = args.output.with_suffix(f".run{run + 1}.log")
        log.write_bytes(output)
        if process.returncode:
            raise RuntimeError(f"Benchmark exited {process.returncode}; see {log}")
        cases = []
        for line in output.decode("utf-8", errors="replace").splitlines():
            if "network-bench " in line:
                cases.append(dict(re.findall(r"(\S+)=([\w.+-]+)", line.split("network-bench ", 1)[1])))
            elif "network-io " in line and cases:
                cases[-1]["io"] = dict(re.findall(r"(\S+)=(\d+)", line.split("network-io ", 1)[1]))
        if len(cases) != 10:
            raise RuntimeError(f"Expected 10 benchmark cases, found {len(cases)}; see {log}")
        records.append({"run": run // 2 + 1, "mode": mode, "wall_seconds": wall, "cpu_seconds": cpu, "cases": cases})
    args.output.write_text(json.dumps({"platform": os.name, "runs": records}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
