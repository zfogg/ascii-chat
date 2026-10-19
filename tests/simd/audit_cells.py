"""Verify vector instructions in each backend's compiled pixel stages."""
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
