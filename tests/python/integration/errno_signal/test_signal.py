import pytest
from types import SimpleNamespace
"""Verify separate diagnostics workers and SIGUSR1 reports on stdout (Linux)."""

from ascii_chat_tests.cli import start_cli

import os
from pathlib import Path
import re
import select
import signal
import sys
import subprocess
import time


if sys.platform != "win32":
    import pty


from ascii_chat_tests.process import free_port as allocate_port


pytestmark = [pytest.mark.integration, pytest.mark.network, pytest.mark.skipif(sys.platform != "linux", reason="Linux signal fixture")]


def free_port():
    return str(allocate_port())


def test_errno_signal_stdout(binary, tmp_path, request):
    probe = request.config.getoption("--errno-probe")
    args = SimpleNamespace(binary=binary, errno_probe=Path(probe) if probe else None)
    artifacts = tmp_path
    master, slave = pty.openpty()
    captured = bytearray()
    env = dict(os.environ, TERM='xterm-256color')
    env.pop('CLAUDECODE', None)
    with (artifacts / 'stderr.log').open('wb') as stderr:
        proc = start_cli(str(args.binary.resolve()), '--log-level', 'debug',
                                 'server', '127.0.0.1', '--port', free_port(), '--websocket-port', free_port(),
                                 '--status-screen=false', stdin=slave, stdout=slave, stderr=stderr, env=env)
        os.close(slave)
        def wait_for(predicate):
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                assert proc.poll() is None, ('server exited', proc.returncode, artifacts)
                if select.select([master], [], [], 0.1)[0]:
                    captured.extend(os.read(master, 65536))
                if predicate():
                    return
            raise AssertionError(('timed out', artifacts, captured[-2000:]))
        try:
            wait_for(lambda: b'Listening' in captured)
            assert b'SYNC_STATE:' not in captured
            for count in (1, 2):
                proc.send_signal(signal.SIGUSR1)
                wait_for(lambda: captured.count(b'SYNC_STATE:') >= count and
                         captured.count(b'Pending error stacks') >= count)
            plain = re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]', b'', captured)
            assert re.search(rb'\[thread/debug_sync\.\d+\].*SYNC_STATE:', plain), plain[-2000:]
            assert re.search(rb'\[thread/debug_stats\.\d+\].*Diagnostic statistics:', plain), plain[-2000:]
            assert re.search(rb'\[thread/debug_errno\.\d+\].*Errno stacks:', plain), plain[-2000:]
            assert b'Mutex ' in captured
            assert b'SYNC_STATE:' not in (artifacts / 'stderr.log').read_bytes()
            print('PASS: distinct debug_sync/debug_stats/debug_errno threads; two SIGUSR1 requests printed reports on stdout')
        finally:
            proc.terminate()
            try:
                deadline = time.monotonic() + 10
                while proc.poll() is None and time.monotonic() < deadline:
                    if select.select([master], [], [], 0.1)[0]:
                        try:
                            captured.extend(os.read(master, 65536))
                        except OSError:
                            break
                assert proc.wait(timeout=1) == 0, ("shutdown failed", proc.returncode, artifacts)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                raise
            finally:
                while select.select([master], [], [], 0)[0]:
                    try:
                        chunk = os.read(master, 65536)
                    except OSError:
                        break
                    if not chunk:
                        break
                    captured.extend(chunk)
                os.close(master)
                (artifacts / 'stdout.log').write_bytes(captured)
                print('Artifacts:', artifacts)

    if args.errno_probe:
        master, slave = pty.openpty()
        captured = bytearray()
        sent = False
        with (artifacts / 'pending-stderr.log').open('wb') as stderr:
            proc = subprocess.Popen([str(args.errno_probe.resolve()), '--signal-stacks'],
                                    stdin=slave, stdout=slave, stderr=stderr, env=env)
            os.close(slave)
            try:
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if select.select([master], [], [], 0.1)[0]:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            break
                        if not data:
                            break
                        captured.extend(data)
                    if not sent and b'ERRNO_PROBE_READY' in captured:
                        proc.send_signal(signal.SIGUSR1)
                        sent = True
                assert proc.wait(timeout=2) == 0, (proc.returncode, artifacts)
                plain = re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]', b'', captured)
                assert sent and b'Pending error stacks (2 threads):' in plain, plain[-2000:]
                for producer in ('producer-a', 'producer-b'):
                    for frame in ('root', 'wrapper'):
                        assert f'{producer} {frame}'.encode() in plain, plain[-2000:]
                print('PASS: SIGUSR1 printed all four pending frames from two threads without clearing them')
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                os.close(master)
                (artifacts / 'pending-stdout.log').write_bytes(captured)
