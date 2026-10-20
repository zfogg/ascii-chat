"""Windows live TCP/WebSocket/WebRTC FPS regression using pywinpty.

Run: python tests/integration/network_fps_windows.py --binary build/bin/ascii-chat.exe --output build/network-fps
Requires: python -m pip install pywinpty
Measures the application's completed-presentation FPS overlay, with normal logging.
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import statistics
import subprocess
import threading
import time
from winpty import PtyProcess


def set_high_priority(pid):
    """Isolate an explicitly requested benchmark from normal-priority host work."""
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.SetPriorityClass.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.SetPriorityClass.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x200, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        if not kernel.SetPriorityClass(handle, 0x80):
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel.CloseHandle(handle)


def stop_client(client):
    """Wait on the owned Windows child, rather than the ConPTY agent's liveness."""
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1000 | 0x100000 | 1, False, client.pid)
    if not handle:
        error = ctypes.get_last_error()
        if error == 87:  # The child already exited and its PID no longer exists.
            return
        raise ctypes.WinError(error)
    try:
        status = kernel.WaitForSingleObject(handle, 0)
        if status == 0:
            return
        if status != 258:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            client.sendintr()
        except (EOFError, OSError):
            pass
        status = kernel.WaitForSingleObject(handle, 3000)
        if status == 258:
            if not kernel.TerminateProcess(handle, 1):
                raise ctypes.WinError(ctypes.get_last_error())
            assert kernel.WaitForSingleObject(handle, 5000) == 0, 'CLI child did not exit'
        elif status != 0:
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel.CloseHandle(handle)


def unused_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def run(options, transport):
    output = options.output / transport
    output.mkdir(parents=True, exist_ok=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('ASCII_CHAT_')}
    env.update(ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK='1',
               ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y', TERM='xterm-256color', HOME=str(output),
               USERPROFILE=str(output), APPDATA=str(output), XDG_CONFIG_HOME=str(output))
    servers, files, commands = [], [], []
    client = None
    reader = None
    stopped = threading.Event()
    samples = []
    tail = ['']
    started = time.monotonic()

    def command(name, flags, level='warn'):
        level = os.environ.get('ASCII_CHAT_TEST_LOG_LEVEL', level)
        (output / (name + '.log')).write_text('', encoding='utf-8')
        argv = [str(options.binary), '--no-check-update', '--log-level', level,
                '--log-file', str(output / (name + '.log'))] + flags
        commands.append(argv)
        return argv

    def server(name, flags, level='warn'):
        dest = open(output / (name + '.stderr'), 'wb')
        files.append(dest)
        process = subprocess.Popen(command(name, flags, level), env=env, stdout=dest, stderr=dest,
                                   stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
        servers.append(process)
        if options.priority == 'high':
            set_high_priority(process.pid)
        return process

    tcp, ws, acds, acds_ws = [unused_port() for _ in range(4)]
    try:
        server_flags = ['server', '0.0.0.0' if transport == 'webrtc' else '127.0.0.1',
                        '--port', str(tcp), '--websocket-port', str(ws), '--status-screen=false']
        if transport in ('webrtc', 'discovery-tcp'):
            # Public RFC 8032 vector, used only by this isolated fixture.
            identity = output / 'acds-test-key'
            identity.write_bytes(bytes.fromhex(
                '9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60'
                'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a'))
            server('acds', ['discovery-service', '127.0.0.1', '--port', str(acds),
                           '--key', str(identity),
                           '--websocket-port', str(acds_ws), '--database', str(output / 'acds.db'),
                           '--status-screen=false'])
            time.sleep(1)
            server_flags += ['--password', 'fps-test-password', '--discovery',
                             '--discovery-service', '127.0.0.1', '--discovery-service-port', str(acds)]
            if transport == 'webrtc':
                server_flags += ['--webrtc']
            else:
                server_flags += ['--discovery-expose-ip']
        host = server('server', server_flags, 'info')
        if transport in ('webrtc', 'discovery-tcp'):
            deadline = time.monotonic() + 25
            session = None
            while time.monotonic() < deadline:
                log = (output / 'server.stderr').read_text(encoding='utf-8', errors='replace')
                match = re.search(r'Session created: ([a-z]+-[a-z]+-[a-z]+)', log)
                if match:
                    session = match[1]
                    break
                if host.poll() is not None:
                    raise AssertionError('WebRTC server exited during registration')
                time.sleep(.1)
            assert session, 'No discovery session created'
            flags = [session, '--discovery-service', '127.0.0.1', '--discovery-service-port', str(acds),
                     '--password', 'fps-test-password']
            if transport == 'webrtc':
                flags += ['--prefer-webrtc']
            else:
                flags += ['--no-webrtc']
        else:
            time.sleep(1)
            flags = ['client', f'{"tcp" if transport == "tcp" else "ws"}://127.0.0.1:{tcp if transport == "tcp" else ws}',
                     '--video-codec', 'raw']
        flags += ['--test-pattern', '--fps', str(options.fps), '--fps-counter', '--width', str(options.width),
                  '--height', str(options.height), '--color-mode', 'truecolor', '--audio=false', '--splash-screen=false']
        client = PtyProcess.spawn(command('client', flags), env=env, dimensions=(options.height, options.width))
        if options.priority == 'high':
            set_high_priority(client.pid)
        started = time.monotonic()

        def read_output():
            pending = ''
            try:
                while not stopped.is_set():
                    chunk = client.read(4096)
                    tail[0] = (tail[0] + chunk)[-1000000:]
                    pending += chunk
                    # Require the terminating escape so numbers split between reads are not miscounted.
                    matches = list(re.finditer(r'FPS:\s*(\d+)\x1b', pending))
                    if matches:
                        samples.append((time.monotonic() - started, int(matches[-1][1])))
                        pending = pending[matches[-1].end() - 1:]
                    pending = pending[-4096:]
            except (EOFError, OSError):
                pass

        reader = threading.Thread(target=read_output, daemon=True)
        reader.start()
        first_frame = None
        measurement_start = None
        deadline = started + options.seconds + 60
        while time.monotonic() < deadline:
            assert client.isalive(), 'Client exited during measurement'
            assert all(p.poll() is None for p in servers), 'Fixture exited during measurement'
            positive = next((t for t, value in samples if value > 0), None)
            if first_frame is None and positive is not None:
                first_frame = positive
                measurement_start = first_frame + 5
            if measurement_start is not None and time.monotonic() - started >= measurement_start + options.seconds:
                break
            time.sleep(.1)
        assert measurement_start is not None, 'No rendered frames observed'
        # Take one rolling counter reading per second to avoid weighting fast readers more heavily.
        buckets = {}
        for elapsed, value in samples:
            if measurement_start <= elapsed < measurement_start + options.seconds:
                buckets[int(elapsed - measurement_start)] = value
        values = list(buckets.values())
        result = dict(transport=transport, priority=options.priority, target_fps=options.fps, width=options.width, height=options.height,
                      samples_by_second=buckets, commands=commands, first_frame_s=first_frame,
                      median_fps=statistics.median(values) if values else 0, minimum_fps=min(values) if values else 0,
                      maximum_fps=max(values) if values else 0)
        (output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        (output / 'terminal.ansi').write_text(tail[0], encoding='utf-8')
        print(json.dumps({k: v for k, v in result.items() if k != 'commands'}), flush=True)
        assert len(values) >= options.seconds * .8, 'Insufficient sustained FPS samples'
        assert statistics.median(values) >= options.fps * .98, 'Median presentation FPS below target'
        assert sum(value >= options.fps * .95 for value in values) >= len(values) * .9, 'Unstable presentation FPS'
        server_log = (output / 'server.log').read_text(encoding='utf-8', errors='replace')
        assert 'state=changing' in server_log, 'Server did not observe changing client video'
        if transport == 'webrtc':
            assert 'Authenticated WebRTC connection adopted' in server_log
        elif transport == 'discovery-tcp':
            assert 'Authenticated WebRTC connection adopted' not in server_log, 'TCP case adopted WebRTC'
            assert 'type=1' in server_log, 'TCP transport was not observed'
    finally:
        try:
            if client:
                stop_client(client)
        finally:
            stopped.set()
            if reader:
                reader.join(timeout=2)
            for process in reversed(servers):
                if process.poll() is None:
                    process.kill()
                process.wait()
            for file in files:
                file.close()
            (output / 'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
            (output / 'terminal.ansi').write_text(tail[0], encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--width', type=int, default=120)
    parser.add_argument('--height', type=int, default=40)
    parser.add_argument('--fps', type=int, default=60)
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--priority', choices=['normal', 'high'], default='normal')
    parser.add_argument('--transport', choices=['all', 'tcp', 'websocket', 'webrtc', 'discovery-tcp'], default='all')
    options = parser.parse_args()
    assert os.name == 'nt', 'This runner requires Windows ConPTY'
    assert options.seconds >= 10 and options.fps > 0 and options.width > 0 and options.height > 0
    options.binary = options.binary.resolve()
    options.output = options.output.resolve()
    if options.priority == 'high':
        set_high_priority(os.getpid())
    for transport in ['tcp', 'websocket', 'webrtc'] if options.transport == 'all' else [options.transport]:
        run(options, transport)


if __name__ == '__main__':
    main()
