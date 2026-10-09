"""Native CLI and shared-library checks, including deterministic source PNGs.

python tests/platform/test_pattern.py --binary build/bin/ascii-chat.exe --library build/bin/asciichat.dll
"""
import argparse
import ctypes as c
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def png(path, width, height, rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + rgb[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def library_checks(path, output):
    directory = os.add_dll_directory(str(path.parent)) if os.name == "nt" else None
    lib = c.CDLL(str(path))
    for name in ["rgb_to_16color", "rgb_to_256color"]:
        fn = getattr(lib, name); fn.argtypes = [c.c_uint8]*3; fn.restype = c.c_uint8
    for name in ["get_16color_rgb", "get_256color_rgb"]:
        getattr(lib, name).argtypes = [c.c_uint8] + [c.POINTER(c.c_uint8)]*3
    def expand(fn, index):
        values = [c.c_uint8() for _ in range(3)]
        fn(index, *[c.byref(v) for v in values])
        return tuple(v.value for v in values)
    for i in range(16, 256):
        rgb = expand(lib.get_256color_rgb, i)
        assert expand(lib.get_256color_rgb, lib.rgb_to_256color(*rgb)) == rgb, i
    for i in range(16):
        assert lib.rgb_to_16color(*expand(lib.get_16color_rgb, i)) == i
    assert lib.rgb_to_256color(0,0,0) == 16
    assert lib.rgb_to_256color(255,255,255) == 231
    class Image(c.Structure):
        _fields_ = [("w",c.c_int),("h",c.c_int),("pixels",c.c_void_p),("alloc_method",c.c_uint8)]
    lib.test_pattern_create.argtypes = [c.c_int,c.c_int,c.POINTER(c.c_void_p)]
    lib.test_pattern_resize.argtypes = [c.c_void_p,c.c_int,c.c_int]
    lib.test_pattern_render.argtypes = [c.c_void_p,c.c_int,c.c_double,c.c_bool]
    lib.test_pattern_image.argtypes = [c.c_void_p]; lib.test_pattern_image.restype = c.POINTER(Image)
    lib.test_pattern_destroy.argtypes = [c.c_void_p]
    a,b=c.c_void_p(),c.c_void_p()
    assert lib.test_pattern_create(640,480,c.byref(a)) == 0
    assert lib.test_pattern_create(640,480,c.byref(b)) == 0
    try:
        for index in [0,1]:
            assert lib.test_pattern_render(a,index,2000,False) == 0
            assert lib.test_pattern_render(b,index,2000,False) == 0
            ia,ib=lib.test_pattern_image(a).contents,lib.test_pattern_image(b).contents
            assert ia.pixels != ib.pixels
            rgb=c.string_at(ia.pixels,640*480*3)
            assert rgb == c.string_at(ib.pixels,640*480*3)
            png(output / f"native-pattern-{index}.png",640,480,rgb)
            assert lib.test_pattern_render(b,index,2500,False) == 0
            assert rgb != c.string_at(ib.pixels,640*480*3)
        for width,height in [(1,1),(20,20),(80,48),(1920,1080)]:
            assert lib.test_pattern_resize(a,width,height) == 0
            pointer=lib.test_pattern_image(a).contents.pixels
            assert lib.test_pattern_resize(a,width,height) == 0
            assert pointer == lib.test_pattern_image(a).contents.pixels
            for index in [0,1]:
                assert lib.test_pattern_render(a,index,2000,False) == 0
                if width in [20,80]: png(output/f"native-{width}x{height}-{index}.png",width,height,c.string_at(pointer,width*height*3))
        assert lib.test_pattern_render(a,2,0,False) != 0
        assert lib.test_pattern_render(a,0,float("nan"),False) != 0
        assert lib.test_pattern_render(a,0,-1,False) != 0
        assert lib.test_pattern_resize(a,0,10) != 0
    finally:
        lib.test_pattern_destroy(a); lib.test_pattern_destroy(b)
    if directory: directory.close()
    return {"palette_entries":256,"independent_sources":True,"resize_and_validation":True}


def cli_checks(binary):
    env=os.environ.copy()
    for key in list(env):
        if key.startswith("ASCII_CHAT_") or key == "WEBCAM_DISABLED": env.pop(key)
    def run(args,extra_env=None):
        with tempfile.TemporaryDirectory() as logs:
            return subprocess.run([str(binary),"--no-check-update","--log-file",str(Path(logs)/"run.log"),*args],env={**env,**(extra_env or {})},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
    common=["--snapshot","--snapshot-delay","0","--audio=false","--splash-screen=false"]
    cases=0
    for selector in [[],["0"],["1"],["=0"],["=1"]]:
        flag=["--test-pattern"+selector[0]] if selector and selector[0].startswith("=") else ["--test-pattern",*selector]
        for color in ["16","256","truecolor"]:
            for width,height in [(20,10),(80,24)]:
                r=run(["mirror",*flag,*common,"--color-mode",color,"--width",str(width),"--height",str(height)])
                assert r.returncode == 0 and r.stdout, (flag,color,width,r.returncode,r.stderr[-500:])
                cases+=1
    compare=[*common,"--width","80","--height","24","--strip-ansi"]
    bare=run(["mirror","--test-pattern",*compare])
    zero=run(["mirror","--test-pattern=0",*compare])
    one=run(["mirror","--test-pattern=1",*compare])
    repeated=run(["mirror","--test-pattern=1","--test-pattern",*compare])
    assert bare.returncode == zero.returncode == one.returncode == repeated.returncode == 0
    assert bare.stdout == zero.stdout == repeated.stdout
    assert one.stdout != zero.stdout
    for value in ["2","-1","bad","1.5",""]:
        r=run(["mirror",f"--test-pattern={value}",*common])
        assert r.returncode != 0, value
    # Legacy environment remains an enable switch; CLI zero still enables pattern zero.
    for extra in [{"WEBCAM_DISABLED":"1"},{"WEBCAM_DISABLED":"true"}]:
        assert run(["mirror",*common,"--width","80","--height","24"],extra).returncode == 0
    assert run(["mirror","--test-pattern","0",*common],{"WEBCAM_DISABLED":"0"}).returncode == 0
    with tempfile.TemporaryDirectory() as tmp:
        config=Path(tmp)/"config.toml"
        for value in ["true","0","1"]:
            config.write_text(f"[webcam]\ntest_pattern = {value}\n")
            r=run(["--config",str(config),"mirror",*common,"--width","80","--height","24"])
            assert r.returncode == 0, (value,r.stderr[-500:])
    return {"cli_snapshots":cases,"invalid_selectors":5,"legacy_environment":True,"config":True}


if __name__ == "__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--binary",type=Path,required=True);parser.add_argument("--library",type=Path);parser.add_argument("--output",type=Path,default=Path("build/test-pattern-evidence"));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    result=cli_checks(args.binary.resolve())
    if args.library: result.update(library_checks(args.library.resolve(),args.output))
    (args.output/"native-results.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(result))
