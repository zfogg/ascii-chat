"""Compile the real mapping implementation against a deterministic fake gateway.

Runs without Criterion, router access, or project dependencies (including Windows).
Usage: python tests/standalone/upnp/run.py [--cc clang]
"""
import argparse
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
HERE = pathlib.Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='clang')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='ascii-chat-upnp-') as tmp:
    out = pathlib.Path(tmp)
    headers = {
        'ascii-chat/common.h': 'common.h',
        'ascii-chat/log/log.h': 'common.h',
        'ascii-chat/util/time.h': 'common.h',
        'miniupnpc/miniupnpc.h': 'gateway.h',
        'miniupnpc/upnpcommands.h': 'gateway.h',
        'miniupnpc/upnperrors.h': 'gateway.h',
        'natpmp.h': 'gateway.h',
    }
    for name, source in headers.items():
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(HERE / source, target)
    public_header = out / 'ascii-chat/network/nat/upnp.h'
    public_header.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'include/ascii-chat/network/nat/upnp.h', public_header)
    for variant, defines in [
        ('upnp-5arg', ['HAVE_MINIUPNPC']),
        ('upnp-7arg', ['HAVE_MINIUPNPC', 'MINIUPNPC_GETVALIDIGD_7ARG']),
        ('natpmp', ['HAVE_MINIUPNPC', 'MINIUPNPC_GETVALIDIGD_7ARG', '__APPLE__']),
        ('unavailable', []),
    ]:
        exe = out / (variant + ('.exe' if __import__('os').name == 'nt' else ''))
        command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(out),
                   *['-D' + flag for flag in defines],
                   str(ROOT / 'lib/network/nat/upnp.c'), str(HERE / 'test.c'), '-o', str(exe)]
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)
        print(f'PASS: {variant}', flush=True)
