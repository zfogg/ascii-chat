"""Verify separate diagnostics workers and SIGUSR1 reports on stdout (Linux)."""
import argparse
import os
from pathlib import Path
import pty
import re
import select
import signal
import socket
import subprocess
import tempfile
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return str(sock.getsockname()[1])


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--errno-probe', type=Path)
    args = parser.parse_args()
    artifacts = Path(tempfile.mkdtemp(prefix='ascii-debug-signal-'))
    master, slave = pty.openpty()
    captured = bytearray()
    env = dict(os.environ, TERM='xterm-256color')
    env.pop('CLAUDECODE', None)
    with (artifacts / 'stderr.log').open('wb') as stderr:
        proc = subprocess.Popen([str(args.binary.resolve()), '--no-check-update', '--log-level', 'debug',
                                 'server', '127.0.0.1', '--port', free_port(), '--websocket-port', free_port(),
                                 '--status-screen=false'], stdin=slave, stdout=slave, stderr=stderr, env=env)
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


if __name__ == '__main__':
    main()
