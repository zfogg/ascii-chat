"""Embed only ICU's UTS #46 normalization data for the Emscripten ICU port.

The port supplies stubdata rather than the ICU data package. Read its own cached
CmnD package, retaining the original version/header and one aligned TOC entry.
No Unicode tables or generated binaries are checked into the repository.
"""
import argparse
from pathlib import Path
import struct
import sys


def extract_idna(package):
    data = package.read_bytes()
    if data[2:4] != b"\xda\x27" or data[8:10] != b"\0\0" or data[12:16] != b"CmnD":
        raise ValueError("Expected a little-endian ASCII ICU common-data package")
    header_size = struct.unpack_from("<H", data)[0]
    count = struct.unpack_from("<I", data, header_size)[0]
    entries = [struct.unpack_from("<II", data, header_size + 4 + i * 8) for i in range(count)]
    matches = []
    for name_offset, offset in entries:
        name_start = header_size + name_offset
        name = data[name_start:data.index(b"\0", name_start)]
        if name.endswith(b"/uts46.nrm"):
            matches.append((name, offset))
    if len(matches) != 1:
        raise ValueError("ICU package must contain exactly one uts46.nrm resource")
    name, offset = matches[0]
    end = min((other for _, other in entries if other > offset), default=len(data) - header_size)
    resource = data[header_size + offset:header_size + end]
    name_offset = 12
    data_offset = (name_offset + len(name) + 1 + 15) & ~15
    toc = struct.pack("<III", 1, name_offset, data_offset) + name + b"\0"
    toc += bytes(data_offset - len(toc))
    return data[:header_size] + toc + resource


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emscripten", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.emscripten))
    from tools import config
    data_dir = Path(config.PORTS) / "icu" / "icu" / "source" / "data" / "in"
    packages = list(data_dir.glob("icudt*l.dat"))
    if len(packages) != 1:
        raise ValueError("Build Emscripten's ICU port before embedding its IDNA data")
    data = extract_idna(packages[0])
    lines = ["/* Generated from the Emscripten ICU port's Unicode data. */",
             "_Alignas(16) const unsigned char asciichat_icu_idna_data[] = {"]
    for start in range(0, len(data), 16):
        lines.append("  " + ", ".join(f"0x{value:02x}" for value in data[start:start + 16]) + ",")
    lines.append("};")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    content = "\n".join(lines) + "\n"
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != content:
        args.output.write_text(content, encoding="utf-8")


if __name__ == "__main__":
    main()
