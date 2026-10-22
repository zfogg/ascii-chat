"""Resolve local symbols without contacting an inherited debuginfod server."""
import argparse
import ctypes as c
import http.server
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import uuid


def worker(library, fixture, name):
    api = c.CDLL(str(library))
    api.log_init.argtypes = [c.c_char_p, c.c_int, c.c_bool, c.c_bool]
    api.log_init(None, 4, True, False)
    api.symbol_cache_init.restype = c.c_int
    api.symbol_cache_resolve_batch.argtypes = [c.POINTER(c.c_void_p), c.c_int]
    api.symbol_cache_resolve_batch.restype = c.POINTER(c.c_char_p)
    api.symbol_cache_free_symbols.argtypes = [c.POINTER(c.c_char_p)]
    api.symbol_cache_free_symbols.restype = None
    api.symbol_cache_insert.argtypes = [c.c_void_p, c.c_char_p]
    api.symbol_cache_insert.restype = c.c_bool
    api.symbol_cache_destroy.restype = None
    probe = c.CDLL(str(fixture))
    address = c.cast(getattr(probe, name), c.c_void_p).value
    assert api.symbol_cache_init() == 0
    symbols = api.symbol_cache_resolve_batch((c.c_void_p * 1)(address), 1)
    assert symbols and name.encode() in symbols[0], "Local function symbol was lost"
    api.symbol_cache_free_symbols(symbols)
    replacement = (name + '_cached_replacement').encode()
    assert api.symbol_cache_insert(address, replacement)
    symbols = api.symbol_cache_resolve_batch((c.c_void_p * 1)(address), 1)
    assert symbols and symbols[0] == replacement, "Cache replacement was not preserved"
    api.symbol_cache_free_symbols(symbols)
    api.symbol_cache_destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library', type=Path)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--symbolizer', default='llvm-symbolizer')
    parser.add_argument('--worker', nargs=2, metavar=('FIXTURE', 'NAME'))
    args = parser.parse_args()
    if args.worker:
        worker(args.library.resolve(), Path(args.worker[0]), args.worker[1])
        return
    symbolizer = shutil.which(args.symbolizer)
    if not symbolizer:
        print('SKIP: llvm-symbolizer is required')
        raise SystemExit(77)
    requests = []
    release = threading.Event()
    class UnavailableDebugServer(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            release.wait(10)
            self.send_response(404)
            self.end_headers()
        def log_message(self, *_):
            pass
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), UnavailableDebugServer)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='ascii-offline-symbols-') as tmp:
            root = Path(tmp)
            name = 'local_symbol_' + uuid.uuid4().hex
            source = root / 'fixture.c'
            fixture = root / 'fixture.so'
            source.write_text(f'int {name}(void) {{ return 42; }}\n')
            # A unique build ID prevents cached debug data masking a remote lookup.
            compiler_env = {k:v for k,v in os.environ.items() if k != 'LD_PRELOAD'}
            subprocess.run([args.cc, '-shared', '-fPIC', '-g0', '-Wl,--build-id=sha1',
                            str(source), '-o', str(fixture)], check=True, timeout=30, env=compiler_env)
            env = {**os.environ, 'LLVM_SYMBOLIZER_PATH': symbolizer,
                   'DEBUGINFOD_URLS': f'http://127.0.0.1:{server.server_port}',
                   'DEBUGINFOD_CACHE_PATH': str(root / 'cache')}
            command = [sys.executable, str(Path(__file__).resolve()), str(args.library.resolve()),
                       '--worker', str(fixture), name]
            process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       start_new_session=True)
            # Reject any remote lookup immediately. Allow process teardown to
            # save instrumented counters without charging it to symbolization.
            deadline = time.monotonic() + 30
            while True:
                try:
                    stdout, stderr = process.communicate(timeout=0.1)
                    break
                except subprocess.TimeoutExpired as error:
                    if requests or time.monotonic() >= deadline:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.communicate()
                        if requests:
                            raise AssertionError(f'Symbolization made remote debug-data requests: {requests}') from error
                        raise AssertionError('Offline symbolizer worker did not finish teardown within 30 seconds') from error
            assert process.returncode == 0, stderr.decode(errors='replace')[-4000:]
            assert not requests, f'Symbolization made remote debug-data requests: {requests}'
            print('PASS: local function resolved with no debuginfod requests')
    finally:
        release.set()
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == '__main__':
    main()
