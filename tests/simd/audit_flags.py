"""Reject optional ISA flags outside their isolated first-party backend sources."""
import json
from pathlib import Path
import re
import sys


def main():
    entries = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    checked = 0
    isolated = set()
    for entry in entries:
        source = entry["file"].replace("\\", "/")
        if any(part in source for part in ("/.deps-cache/", "/_deps/", "/deps/", "/vcpkg_installed/")):
            continue
        checked += 1
        command = entry.get("command", " ".join(entry.get("arguments", [])))
        match = re.search(r"/ascii/(sse2|ssse3|avx2|neon|sve)/", source)
        backend = match.group(1) if match else None
        allowed = {"-march=x86-64", "-march=armv8-a", "-msse2", "-mno-mmx"}
        if backend:
            isolated.add(backend)
            allowed.add({"sse2": "-msse2", "ssse3": "-mssse3", "avx2": "-mavx2",
                         "neon": "-mfpu=neon", "sve": "-march=armv8-a+sve"}[backend])
            if "-fno-lto" not in command or "include-pch" in command:
                raise RuntimeError(f"Backend must be isolated from LTO/PCH: {source}")
        for flag in re.findall(r"(?<!\S)(-m(?:arch|cpu|fpu)=[^\s\"]+|-m(?:avx|sse|ssse)[^\s\"]*)", command):
            if flag not in allowed:
                raise RuntimeError(f"Nonportable flag {flag} in {source}")
    if not checked:
        raise RuntimeError("No first-party compile commands audited")
    print(f"Audited {checked} first-party compile commands; isolated backends: {', '.join(sorted(isolated))}")


if __name__ == "__main__":
    main()
