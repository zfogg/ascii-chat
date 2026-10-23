"""Verify vector instructions in each backend's compiled pixel stages."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
import re
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary")
parser.add_argument("backend",choices=["sse2","ssse3","avx2","neon","sve"])
parser.add_argument("--objdump",default="objdump")
args=parser.parse_args()
assembly=subprocess.check_output([args.objdump,"-d",args.binary],text=True)
functions={}
current=None
for line in assembly.splitlines():
    match=re.match(r"^[0-9a-f]+ <([^>]+)>:",line)
    if match:
        current=match[1]
        functions[current]=[]
    elif current:
        functions[current].append(line)
patterns={
    "sse2": {"pack":r"\bpslld\b", "lookup":r"\bpcmpgtd\b", "quantize":r"\bpmul", "run":r"\bpcmpeq", "halfblock":r"\bpmul|\bpmadd|\bpcmpeq"},
    "ssse3":{"pack":r"\bpshufb\b", "lookup":r"\bpshufb\b", "quantize":r"\bpmul", "run":r"\bpcmpeq", "halfblock":r"\bpmul|\bpmadd|\bpcmpeq"},
    "avx2": {"pack":r"\bvpshufb\b", "lookup":r"\bvpgatherdd\b", "quantize":r"\bvpmul", "run":r"\bvpcmpeq", "halfblock":r"\bvpmul|\bvpmadd|\bvpcmpeq"},
    "neon": {"pack":r"\bld3\b", "lookup":r"\btbl\b", "quantize":r"\bmul\s+v", "run":r"\bcmeq\s+v", "halfblock":r"\bmul\s+v|\bshl\s+v"},
    "sve":  {"pack":r"\bld3b\b", "lookup":r"\bld1w\b.*z[0-9]+\.s", "quantize":r"\bmul\s+z", "run":r"\bcmpeq\s+p", "halfblock":r"\bmul\s+z|\blsl\s+z"},
}
for stage,pattern in patterns[args.backend].items():
    name=f"simd_{stage}_{args.backend}"
    lines=functions.get(name,[])
    evidence=next((line.strip() for line in lines if re.search(pattern,line)),None)
    if evidence is None:
        raise SystemExit(f"{name}: expected vector instructions were not found")
    print(f"{name}: {evidence}")

# Check the retained ISA-owned row arithmetic, not just separately linked kernels.
row_name = "avx2_compute_luminance_32" if args.backend == "avx2" else f"simd_luminance_{args.backend}"
row_pattern = {
    "sse2": r"\bpmullw\b", "ssse3": r"\bpmullw\b", "avx2": r"\bvpmullw\b",
    "neon": r"\bumull\b", "sve": r"\bmul\s+z",
}[args.backend]
evidence = next((line.strip() for line in functions.get(row_name, []) if re.search(row_pattern, line)), None)
if evidence is None:
    raise SystemExit(f"{row_name}: retained vector row arithmetic was not found")
print(f"{row_name}: {evidence}")
if args.backend == "neon":
    for instruction in ("st2", "st3", "st4"):
        evidence = next((line.strip() for line in functions.get("simd_emit_mono_neon", [])
                         if re.search(r"\b" + instruction + r"\b", line)), None)
        if evidence is None:
            raise SystemExit(f"NEON UTF-8 compaction: {instruction} was not found")
        print(f"simd_emit_mono_neon: {evidence}")
